#include "robotArmControl.h"

robotArmControl *ptr;
uStepperS stepper;
float rotHome, leftHome, rightHome;
state_t state = rdy;
float address = 0.0;
volatile binaryFloat nextCommandArgument;
binaryFloat currentCommandArgument;
volatile state_t nextCommand = rdy;
float supplyVoltage = 12.0;
uint8_t valveCompareMatch = 107;
uint16_t pumpCompareMatch = 254;

robotArmControl::robotArmControl() { ptr = this; }

void robotArmControl::begin() {

#ifdef DEBUG
  DEBUGPORT.begin(115200);
#endif
  // Initiate uStepper
  stepper.setMaxVelocity(20);
  stepper.setMaxAcceleration(1000);
  stepper.setCurrent(50);
  stepper.setup(); // Initiate the stepper object to use closed loop control
  
  stepper.disablePid();

  // Begin I2C communication
  bus.begin();

  if (bus.addressNum != BASE) {
    // Setup for receive and request events for I2C slaves
    I2CPORT.onReceive(this->busReceiveEvent);
    I2CPORT.onRequest(this->busRequestEvent);

    if (bus.addressNum == SHOULDER) {
      DEBUG_PRINTLN("-- I Am Shoulder --");
      this->direction = 1.0;
      stepper.setOrientation(1);
      stepper.encoder.setHome();
      stepper.setHoldCurrent(40);
      this->stallSense = 2;
      this->homeSpeed = 35.0;
    }

    if (bus.addressNum == ELBOW) {
      DEBUG_PRINTLN("-- I Am Elbow --");
      this->direction = -1.0;
      stepper.setHoldCurrent(10);
      delay(2000);
      stepper.setHoldCurrent(40);
      stepper.setOrientation(1);
      stepper.encoder.setHome();
      this->stallSense = 2;
      this->homeSpeed = 35.0;
    }
  }

  // Only begin Serial communication (gcode) on master
  if (bus.addressNum == BASE) {
    // stepper.encoder.setHome();
    comm.begin();
    this->direction = 1.0;
    pinMode(3,INPUT);
    pinMode(2,INPUT); //Set as input initially to make sure pwm output is disable on powerup
    
    DDRB |= (1 << 5);
    TCCR4A = (1 << 1); //WGM41 = 1, VGM40 = 0
    TCCR4B = (1 << 4) | (1 << 3); //WGM43 = 1, WGM42 = 1 (Fast PWM mode, TOP = ICR4)
    ICR4 = 39900;
    TIMSK4 = (1 << 0) | (1 << 1); //enable overflow and OCA interrupts
    OCR4A = 1000;
    TCCR4B |= (1 << 1); //Enable clock at prescaler 8. 16MHz/8 = 2MHz/40000 = 50Hz Servo Pulse frequency
    stepper.setOrientation(1); // match the servo's 4-pin connector orientation
    stepper.encoder.setHome();
    stepper.setHoldCurrent(40);

    this->stallSense = 2;
    this->homeSpeed = 30.0;

    supplyVoltage = (float)analogRead(A2);
    supplyVoltage *= 0.04882813;   //(VRef/1024) * 10   -   10k in series with 1k = 1/10th divider
    valveCompareMatch = (uint8_t)(1280.0/supplyVoltage);
    pumpCompareMatch = (uint8_t)(3072.0/supplyVoltage);

    TCCR2A = (1 << 5) | (1 << 1) |(1 << 0);    //COM2B1 = 1, COM2B0 = 1, WGM21 = 1, WGM20 = 1 (Fast PWM, non-inverting)
    OCR2B = valveCompareMatch; //5V/12V = 0.417  * 256 = 106.752 = 107
    TCCR2B = 1; //prescaler = 8. 2MHz/256 = 7.8kHz PWM
    
    if(pumpCompareMatch > 255)
    {
      pumpCompareMatch = 255;
    }

    TCCR3A = (1 << 5) | (1 << 4)  |(1 << 0);    //COM2B1 = 1, COM2B0 = 1, WGM21 = 1, WGM20 = 1 (Fast PWM, non-inverting)
    OCR3B = pumpCompareMatch; //5V/12V = 0.417  * 256 = 106.752 = 107
    TCCR3B = (1 << 3) | (1 << 1); //prescaler = 8. 2MHz/256 = 7.8kHz PWM
    PORTD &= ~(1 << 2);
    DEBUG_PRINTLN("-- I Am BASE --");
    while (this->bus.requestState(ELBOW) != rdy) {
    DEBUG_PRINTLN("ELBOW NOT RDY ");
    delay(100);
    }
     DEBUG_PRINTLN("-- I Am BASE --");
    while (this->bus.requestState(SHOULDER) != rdy) {
      DEBUG_PRINTLN("SHOULDER NOT RDY ");
      delay(100);
    }
    this->angleBase = stepper.encoder.getAngleMoved();
    this->angleElbow = this->bus.requestAngle(ELBOW);
    this->angleShoulder = this->bus.requestAngle(SHOULDER);

    this->angleTargetBase = this->angleBase;
    this->angleTargetShoulder = this->angleShoulder;
    this->angleTargetElbow = this->angleElbow;
  }
  
  //stepper.enablePid();
  DEBUG_PRINTLN("-- Setup complete --");
}

void robotArmControl::homeArm() {
  uint8_t i;
  

  bus.writeCommand(ELBOW, 's', 5.0);
  bus.writeCommand(SHOULDER, 's', 5.0);
  stepper.setMaxVelocity(5.0);

  bus.writeCommand(ELBOW, 'h', 0);
  for (i = 0; i < 10 && this->bus.requestState(ELBOW) != rdy; i++) {
    delay(100);
  }
  bus.writeCommand(SHOULDER, 'h', 0);
  //homeAxis(CCW);
  stepper.moveToEnd(CCW, this->homeSpeed, this->stallSense, 15000); // Move to stall is detected
  stepper.encoder.setHome();    // Zero encoder position
 
  while (this->bus.requestState(ELBOW) != rdy) {
    DEBUG_PRINTLN("ELBOW NOT RDY ");
    delay(100);
  }
  while (this->bus.requestState(SHOULDER) != rdy) {
    DEBUG_PRINTLN("SHOULDER NOT RDY ");
    delay(100);
  }
  
  stepper.setMaxVelocity(100);
  stepper.moveToAngle(880.0);
  bus.writeCommand(SHOULDER, 's', 5.0);
  delay(300);
  bus.setAngle(SHOULDER, -60.0);
  delay(1000);
  bus.setAngle(ELBOW, -20.0);
  while(stepper.getMotorState());
  stepper.encoder.setHome();    // Zero encoder position
  // stepper.moveToAngle(20.0);
  // bus.writeCommand(ELBOW,'s',HOMEFEEDRATESLOW);
  // bus.writeCommand(SHOULDER,'s',HOMEFEEDRATESLOW);
  // stepper.setMaxVelocity(HOMEFEEDRATESLOW);

  // use homing command on all motors to get to home position
  // This should actually be a gcode for homing like on 3D printers (calling
  // this function with that) take known XYZ and calc motor angles use the calc
  // motor angles as offset - the calc can be done offline so this function
  // actually only has to move motors to home - the defined offset should always
  // be used
}

bool robotArmControl::inRange( float value, float target, float limit ){
  return (abs(target-value) <= limit); 
}

void robotArmControl::masterLoop() 
{
  uint32_t ms = millis();
  bool continous = 1;
  uint8_t jointsAllowedToMove;
  this->movementInProgress = 0;
  while (1) {
    // Listen for commands from GUI or UART depending on what is defined in
    // config
    if (comm.listen() || millis() - lastCommand > 1000) {

      this->lastCommand = millis();

      // Execute the received command
      this->execute(comm.getPacket());
    } 

    else if (millis() - ms > 50) {
      this->angleBase = stepper.encoder.getAngleMoved();
      this->angleElbow = this->bus.requestAngle(ELBOW);
      this->angleShoulder = this->bus.requestAngle(SHOULDER);

      angleToxyz(this->angleBase, this->angleElbow, this->angleShoulder,x,y,z);
      // Check if target is reached
      if(this->targetReached == false)
      {
        if(this->movementInProgress == 0)
        {
          this->movementInProgress = 1;
          calcVelocityProfileMovement();
          this->setMotorSpeed(ELBOW,this->targetElbowSpeed);
          this->setMotorSpeed(SHOULDER,this->targetShoulderSpeed);
          this->setMotorSpeed(BASE,this->targetBaseSpeed);
          this->setMotorAngle(BASE,this->angleTargetBase);
          this->setMotorAngle(ELBOW,this->angleTargetElbow);
          this->setMotorAngle(SHOULDER,this->angleTargetShoulder);
        }
        else if(this->baseTargetReached == 1 && this->elbowTargetReached == 1 && this->shoulderTargetReached == 1 )
        {
          //DEBUG_PRINTLN("1");
          if(this->setServo(this->targetServo) == this->targetServo)
          {
            if(this->targetPumpState != this->currentPumpState)
            {
              this->setPump(targetPumpState);
            }
            
            this->targetReached = 1;
            this->movementInProgress = 0;
            comm.send((char *)"REACHED");
          }
          
        }
        else if(this->movementInProgress == 1)
        {
          DEBUG_PRINTLN("2");
          this->baseTargetReached = !stepper.getMotorState();
          if(this->bus.requestState(ELBOW) == rdy)
          {
            this->elbowTargetReached = 1;
          }
          if(this->bus.requestState(ELBOW) == rdy)
          {
            this->shoulderTargetReached = 1;
          }
        }
      }
      else if(this->sx != 0.0 || this->sy != 0.0 || this->sz != 0.0 )
      {
        continous = 1;
        this->tx = this->x + (this->sx * 0.2);
        this->ty = this->y;
        this->tz = this->z + (this->sz * 0.2);
        this->xyzToAngles(this->angleTargetBase, this->angleTargetElbow,
                    this->angleTargetShoulder, tx, ty, tz);

        this->angleTargetBase = this->angleBase + (this->sy * 0.2);
        
        jointsAllowedToMove = calcVelocityProfile(this->angleTargetBase,this->angleTargetElbow,this->angleTargetShoulder);
        
        (jointsAllowedToMove & 0x01) ? this->runContinously(BASE, this->targetBaseSpeed) : stepper.stop(HARD);
        (jointsAllowedToMove & 0x02) ? this->runContinously(SHOULDER, this->targetShoulderSpeed) : this->bus.stopSlave(SHOULDER);
        (jointsAllowedToMove & 0x04) ? this->runContinously(ELBOW, this->targetElbowSpeed) : this->bus.stopSlave(ELBOW);
      }
      else
      {
        if(continous)
        {
          continous = 0;
          this->bus.stopSlave(ELBOW);
          stepper.stop(HARD);
          this->bus.stopSlave(SHOULDER);
        }
      }
      
      ms = millis();
      if(this->valveOn)
      {
        if(ms >= this->valveOnTime)
        {
          this->valveOn = 0;
          DDRD &= ~(1 << 3);    //Turn off valve
        }
      }
    }
    this->setServo();
  }
}

void robotArmControl::slaveLoop() 
{
  while (1) 
  {
    cli();
    state = nextCommand;
    currentCommandArgument.f = nextCommandArgument.f;
    nextCommand = rdy;
    sei();
    if (state == home) {
      stepper.stop(HARD);
      stepper.moveToAngle(stepper.encoder.getAngleMoved());
      
      if (ptr->direction<0) {
        stepper.moveToEnd(CCW, this->homeSpeed, this->stallSense,15000 ); // Move to stall is detected
      } else {
        stepper.moveToEnd(CW, this->homeSpeed, this->stallSense,15000 ); // Move to stall is detected
      }
      stepper.encoder.setHome(); // Zero encoder position
    } else if (state == stop) {
      stepper.stop(HARD);
      stepper.moveToAngle(stepper.encoder.getAngleMoved());
    } else if (state == move) {
      stepper.moveToAngle(ptr->direction * currentCommandArgument.f);
    } else if (state == resetHome) {
      stepper.encoder.setHome(); // Zero encoder position
    } else if (state == setVelocity) {
      stepper.setMaxVelocity(currentCommandArgument.f);
    } else if (state == 5) {
      stepper.setMaxVelocity(currentCommandArgument.f);
      if (ptr->direction * currentCommandArgument.f < 0.0) {
        stepper.runContinous(CW);
      } else {
        stepper.runContinous(CCW);
      }
    } else if (state == setAcceleration) {
      stepper.setMaxAcceleration(currentCommandArgument.f);
      stepper.setMaxDeceleration(currentCommandArgument.f);
    }
    else if (state == setBrakeMode) {
      if(currentCommandArgument.f == 0.0)
      {
        stepper.setBrakeMode(FREEWHEELBRAKE);
      }
      else if(currentCommandArgument.f == 1.0)
      {
        stepper.setBrakeMode(COOLBRAKE);
      }
      else if(currentCommandArgument.f == 2.0)
      {
        stepper.setBrakeMode(HARDBRAKE);
      }
    }
    else if (state == setHomeSpeed) {
      this->homeSpeed = currentCommandArgument.f;
    }
    else if (state == setCurrent) {
      stepper.setCurrent(currentCommandArgument.f);
    }
    else if (state == setHoldCurrent) {
      stepper.setHoldCurrent(currentCommandArgument.f);
    }
    else if (state == setStallSense) {
      this->stallSense = currentCommandArgument.f;
    }
  }
}

void robotArmControl::run() {

  if (bus.addressNum == BASE) 
  {
    this->masterLoop();
  } 
  else 
  {
    this->slaveLoop();
  }
}

void TIMER4_COMPA_vect(void)
{
  asm volatile("cbi 0x05,5\n\t"); 
  asm volatile("reti \n\t");
}
void TIMER4_OVF_vect(void)
{
  asm volatile("sbi 0x05,5\n\t"); 
  asm volatile("reti \n\t");
}

uint8_t robotArmControl::calcVelocityProfile(float baseTarget, float elbowTarget, float shoulderTarget, bool correction) {
  float baseDistance, elbowDistance, shoulderDistance;
  float euclideanDistance;
  float angularFeedrate = FEEDRATETOANGULARFEEDRATE(this->feedrate);
  this->angleBase = ptr->direction * stepper.encoder.getAngleMoved();
  this->angleShoulder = this->bus.requestAngle(SHOULDER);
  this->angleElbow = this->bus.requestAngle(ELBOW);
/*
  DEBUG_PRINT("BaseAngle: ");
  DEBUG_PRINT(this->angleBase);
  DEBUG_PRINT(" ElbowAngle: ");
  DEBUG_PRINT(this->angleElbow);
  DEBUG_PRINT(" ShoulderAngle: ");
  DEBUG_PRINTLN(this->angleShoulder);*/
/*
  DEBUG_PRINT("BaseAngleTarget: ");
  DEBUG_PRINT(this->angleTargetBase);
  DEBUG_PRINT(" ElbowAngleTarget: ");
  DEBUG_PRINT(this->angleTargetElbow);
  DEBUG_PRINT(" ShoulderAngleTarget: ");
  DEBUG_PRINTLN(this->angleTargetShoulder);
*/
  if(correction)
  {
    baseDistance = baseTarget - this->angleBase;
  }
  else 
  {
    if(this->sy == 0.0)
    {
      baseDistance = 0.0;
    }
    else
    {
      baseDistance = baseTarget - this->angleBase;
    }
  }

  elbowDistance = elbowTarget - this->angleElbow;
  shoulderDistance = shoulderTarget - this->angleShoulder;
  DEBUG_PRINT("Elbow: ");
  DEBUG_PRINTLNFLOAT(elbowDistance,3);
  DEBUG_PRINT("Shoulder: ");
  DEBUG_PRINTLNFLOAT(shoulderDistance,3);
  DEBUG_PRINT("Base: ");
  DEBUG_PRINTLNFLOAT(baseDistance,3);
/*
  DEBUG_PRINT("BaseDist: ");
  DEBUG_PRINTFLOAT(baseDistance,5);
  DEBUG_PRINT(" ElbowDist: ");
  DEBUG_PRINTFLOAT(elbowDistance,5);
  DEBUG_PRINT(" ShoulderDist: ");
  DEBUG_PRINTLNFLOAT(shoulderDistance,5);
*/
  euclideanDistance =
      sqrt((baseDistance * baseDistance) + (elbowDistance * elbowDistance) +
           (shoulderDistance * shoulderDistance));

  
  this->targetBaseSpeed = (baseDistance / euclideanDistance) * angularFeedrate;
  this->targetElbowSpeed = (elbowDistance / euclideanDistance) * angularFeedrate;
  this->targetShoulderSpeed = (shoulderDistance / euclideanDistance) * angularFeedrate;
  //DEBUG_PRINTLN(abs(this->angleBase));
  

  //jointsAllowedToMove |= 0x04;
  /*
  DEBUG_PRINT("EuclidianDist: ");
  DEBUG_PRINTLN(euclideanDistance);

  DEBUG_PRINT("BaseSpeed: ");
  DEBUG_PRINT(this->targetBaseSpeed);
  DEBUG_PRINT(" ElbowSpeed: ");
  DEBUG_PRINT(this->targetElbowSpeed);
  DEBUG_PRINT(" ShoulderSpeed: ");
  DEBUG_PRINTLN(this->targetShoulderSpeed);
  DEBUG_PRINT(" jointsAllowedToMove: ");
  DEBUG_PRINTLN(jointsAllowedToMove);
*/
  //bus.writeCommand(ELBOW, 's', this->targetElbowSpeed);
  //bus.writeCommand(SHOULDER, 's', this->targetShoulderSpeed);
  // bus.writeCommand(ELBOW,'i',2000);
  // bus.writeCommand(SHOULDER,'i',shoulderVelocity);
  //stepper.setMaxVelocity(this->targetBaseSpeed);
  /*stepper.setMaxAcceleration(nextCommandArgument.f);
  stepper.setMaxDeceleration(nextCommandArgument.f);*/

  return this->checkLimits(correction);
}

void robotArmControl::calcVelocityProfileMovement(void) {
  float baseDistance, elbowDistance, shoulderDistance;
  float euclideanDistance;
  float angularFeedrate = FEEDRATETOANGULARFEEDRATE(this->feedrate);
  this->angleBase = stepper.encoder.getAngleMoved();
  this->angleShoulder = this->bus.requestAngle(SHOULDER);
  this->angleElbow = this->bus.requestAngle(ELBOW);
/*
  DEBUG_PRINT("BaseAngle: ");
  DEBUG_PRINT(this->angleBase);
  DEBUG_PRINT(" ElbowAngle: ");
  DEBUG_PRINT(this->angleElbow);
  DEBUG_PRINT(" ShoulderAngle: ");
  DEBUG_PRINTLN(this->angleShoulder);*/
/*
  DEBUG_PRINT("BaseAngleTarget: ");
  DEBUG_PRINT(this->angleTargetBase);
  DEBUG_PRINT(" ElbowAngleTarget: ");
  DEBUG_PRINT(this->angleTargetElbow);
  DEBUG_PRINT(" ShoulderAngleTarget: ");
  DEBUG_PRINTLN(this->angleTargetShoulder);
*/
  baseDistance = this->angleTargetBase - this->angleBase;
  elbowDistance = this->angleTargetElbow - this->angleElbow;
  shoulderDistance = this->angleTargetShoulder - this->angleShoulder;
/*
  DEBUG_PRINT("BaseDist: ");
  DEBUG_PRINTFLOAT(baseDistance,5);
  DEBUG_PRINT(" ElbowDist: ");
  DEBUG_PRINTFLOAT(elbowDistance,5);
  DEBUG_PRINT(" ShoulderDist: ");
  DEBUG_PRINTLNFLOAT(shoulderDistance,5);
*/
  euclideanDistance =
      sqrt((baseDistance * baseDistance) + (elbowDistance * elbowDistance) +
           (shoulderDistance * shoulderDistance));

  
  this->targetBaseSpeed = (baseDistance / euclideanDistance) * angularFeedrate;
  this->targetElbowSpeed = (elbowDistance / euclideanDistance) * angularFeedrate;
  this->targetShoulderSpeed = (shoulderDistance / euclideanDistance) * angularFeedrate;

  this->baseTargetReached = 0;
  this->elbowTargetReached = 0;
  this->shoulderTargetReached = 0;
  
}

bool robotArmControl::setServoSpeed(float speed) {
  if (speed <= 0.01 || speed > 100.0)
    return false;
  this->servoSpeed = speed;
  return true;
}

void robotArmControl::setServo() {
  uint16_t servoSetting;
  static int32_t lastRun = millis();
  int32_t timeElapsed = millis() - lastRun;
  if(timeElapsed >= this->servoUpdatePeriod)
  {
    lastRun = millis();
  }
  else
  {
    return;
  }
  
  if(this->filteredServo < this->currentServo)
  {
    this->filteredServo += this->servoSpeed * timeElapsed;
    if(this->filteredServo > this->currentServo)
    {
      this->filteredServo = this->currentServo;
    }
  }
  else if(this->filteredServo > this->currentServo)
  {
    this->filteredServo -= this->servoSpeed * timeElapsed;
    if(this->filteredServo < this->currentServo)
    {
      this->filteredServo = this->currentServo;
    }
  }
  else
  {
    return;
  }
  DEBUG_PRINTLN(this->filteredServo);
  servoSetting = (uint16_t)(this->filteredServo*22.222222222)+1000;
  if(servoSetting < 1000)
  {
    servoSetting = 1000;
  }
  if(servoSetting > 5000)
  {
    servoSetting = 5000;
  }

  OCR4A = servoSetting;
}

float robotArmControl::setServo(float servoVal) {
    DEBUG_PRINT("Servo: ");
    DEBUG_PRINTLN(servoVal);
    this->currentServo = servoVal;
    this->setServo();
    return this->filteredServo;
}

void robotArmControl::setXYZ() {
  
  this->xyzToAngles(this->angleTargetBase, this->angleTargetElbow, this->angleTargetShoulder, tx, ty, tz);
}

void robotArmControl::execute(char *command) {

  if (command == NULL) {
    // If command is null, there is no new packet. Return a RDY to indidate arm
    // is ready for new commands
    comm.send((char *)COMMAND_READY);
    return;
  }

  //DEBUG_PRINT("Got command: ");
  //DEBUG_PRINTLN(command);

  // Check for each valid command

  if (comm.check((char *)"G1")) {
  	float servoSpeed;

    // Extract position, working on the string provided by .check()
    bool px = comm.value((char *)"X", &this->tx);
    bool py = comm.value((char *)"Y", &this->ty);
    bool pz = comm.value((char *)"Z", &this->tz);
    bool pv = comm.value((char *)"F", &this->feedrate);
    bool ps = comm.value((char *)"S", &this->targetServo);
    bool pp = comm.value((char *)"P", &this->targetPumpState);
    bool pa = comm.value((char *)"A", &servoSpeed);

    if ((px || py || pz) != true)
      comm.send((char *)"INVALID POS");
    else {
      DEBUG_PRINT("STARTING MOVE");
      if (pa) {
        this->setServoSpeed(servoSpeed); // TODO check, report error
      }
      this->targetReached = false;
      this->movementInProgress = 0;
      setXYZ();
    }
  }

  else if (comm.check((char *)"M10")) {

    // Extract position, working on the string provided by .check()
    bool sx = comm.value((char *)"X", &this->sx);
    bool sy = comm.value((char *)"Y", &this->sy);
    bool sz = comm.value((char *)"Z", &this->sz);

    if ((sx || sy || sz) != true) {
      comm.send((char *)"INVALID POS");
    }

    else {
      if(this->sx!=0)
      {
        if(this->sx>0 && this->sx<10)
        {
          this->sx=10;
        }
        else if(this->sx<0 && this->sx>-10)
        {
          this->sx=-10;
        }
      }
      if(this->sy!=0)
      {
        if(this->sy>0 && this->sy<10)
        {
          this->sy=10;
        }
        else if(this->sy<0 && this->sy>-10)
        {
          this->sy=-10;
        }
      }
      if(this->sz!=0)
      {
        if(this->sz>0 && this->sz<10)
        {
          this->sz=10;
        }
        else if(this->sz<0 && this->sz>-10)
        {
          this->sz=-10;
        }
      }
      this->feedrate = sqrt((this->sx*this->sx) + (this->sy*this->sy) + (this->sz*this->sz));
      }
  }

  else if (comm.check((char *)"M0")) {
    stepper.stop(HARD);
    this->bus.stopSlave(ELBOW);
    this->bus.stopSlave(SHOULDER);
    // STOP
  }

  else if (comm.check((char *)"M1")) {
    // PAUSE
  }

  else if (comm.check((char *)"M2"))
    // RECORD
    this->isRecording = true;

  else if (comm.check((char *)"M3"))
    // STOP RECORD
    this->isRecording = false;

  else if (comm.check((char *)"M4")) {

    if (!comm.value((char *)"S", &this->targetServo)) {
      comm.send((char *)"INVALID SERVO");
    } else {      
      this->setServo(this->targetServo);

      float servoSpeed;
      if (comm.value((char *)"A", &servoSpeed))
        this->setServoSpeed(servoSpeed);
    }
  }

  else if (comm.check((char *)"M5"))
  {
    // PUMP ON
    this->setPump(true);
    DEBUG_PRINTLN("M5");
  }
    

  else if (comm.check((char *)"M6"))
  {  
    // PUMP OFF
    this->setPump(false);
  }

  else if (comm.check((char *)"M9"))
  {  
    //Send current position in xyz
    this->sendXYZ();
  }

  else if (comm.check((char *)"M14"))
  {  
    //Set brakemode to freewheel
    this->setbrakeMode(BASE,0.0);
    this->setbrakeMode(ELBOW,0.0);
    this->setbrakeMode(SHOULDER,0.0);
  }

  else if (comm.check((char *)"M15"))
  {  
    //Set brakemode to coolbrake
    this->setbrakeMode(BASE,1.0);
    this->setbrakeMode(ELBOW,1.0);
    this->setbrakeMode(SHOULDER,1.0);
  }

  else if (comm.check((char *)"M16"))
  {  
    //Set brakemode to hardbrake
    this->setbrakeMode(BASE,2.0);
    this->setbrakeMode(ELBOW,2.0);
    this->setbrakeMode(SHOULDER,2.0);
  }
  else if (comm.check((char *)"M17"))
  {
    float acceleration;
    if (!comm.value((char *)"A", &acceleration)) 
    {
      comm.send((char *)"INVALID acceleration");
    } 
    else {
      this->setMotorAcceleration(BASE,FEEDRATETOANGULARFEEDRATE(acceleration));
      this->setMotorAcceleration(ELBOW,FEEDRATETOANGULARFEEDRATE(acceleration));
      this->setMotorAcceleration(SHOULDER,FEEDRATETOANGULARFEEDRATE(acceleration));
    }
    
    DEBUG_PRINTLN("M17");
  }
  else if (comm.check((char *)"M18"))
  {
    float sense;
    if (!comm.value((char *)"S", &sense)) 
    {
      comm.send((char *)"INVALID sense");
    } 
    else {
      this->setMotorStallSense(BASE,sense);
    }
    
    DEBUG_PRINTLN("M18");
  }
  else if (comm.check((char *)"M19"))
  {
    float sense;
    if (!comm.value((char *)"S", &sense)) 
    {
      comm.send((char *)"INVALID sense");
    } 
    else {
      this->setMotorStallSense(ELBOW,sense);
    }
    
    DEBUG_PRINTLN("M19");
  }
  else if (comm.check((char *)"M20"))
  {
    float sense;
    if (!comm.value((char *)"S", &sense)) 
    {
      comm.send((char *)"INVALID sense");
    } 
    else {
      this->setMotorStallSense(SHOULDER,sense);
    }
    
    DEBUG_PRINTLN("M20");
  }
  else if (comm.check((char *)"M21"))
  {
    float speed;
    DEBUG_PRINTLN("M21");
    if (!comm.value((char *)"S", &speed)) 
    {
      comm.send((char *)"INVALID speed");
    } 
    else {
      this->setMotorHomingSpeed(BASE,speed);
    }
    
    DEBUG_PRINTLN("M21");
  }
  else if (comm.check((char *)"M22"))
  {
    float speed;
    if (!comm.value((char *)"S", &speed)) 
    {
      comm.send((char *)"INVALID speed");
    } 
    else {
      this->setMotorHomingSpeed(ELBOW,speed);
    }
    
    DEBUG_PRINTLN("M22");
  }
  else if (comm.check((char *)"M23"))
  {
    float speed;
    if (!comm.value((char *)"S", &speed)) 
    {
      comm.send((char *)"INVALID speed");
    } 
    else {
      this->setMotorHomingSpeed(SHOULDER,speed);
    }
    
    DEBUG_PRINTLN((char *)"M23");
  }
  else if (comm.check((char *)"M24")) {
    float current, holdCurrent;

    bool cc = comm.value((char *)"C", &current);
    bool hc = comm.value((char *)"H", &holdCurrent);

    if ((cc || hc) != true) {    
      comm.send((char *)"INVALID currents");
    } 
    else {
      if (cc)
        this->setMotorCurrent(BASE, current);
      if (hc)
        this->setMotorHoldCurrent(BASE, current);
    }
    
    // DEBUG_PRINTLN("M24");
  }  
  else if (comm.check((char *)"M25")) {
    float current, holdCurrent;

    bool cc = comm.value((char *)"C", &current);
    bool hc = comm.value((char *)"H", &holdCurrent);

    if ((cc || hc) != true) {    
      comm.send((char *)"INVALID currents");
    } 
    else {
      if (cc)
        this->setMotorCurrent(ELBOW, current);
      if (hc)
        this->setMotorHoldCurrent(ELBOW, current);
    }
    
    // DEBUG_PRINTLN("M25");
  }  
  else if (comm.check((char *)"M26")) {
    float current, holdCurrent;

    bool cc = comm.value((char *)"C", &current);
    bool hc = comm.value((char *)"H", &holdCurrent);

    if ((cc || hc) != true) {    
      comm.send((char *)"INVALID currents");
    } 
    else {
      if (cc)
        this->setMotorCurrent(SHOULDER, current);
      if (hc)
        this->setMotorHoldCurrent(SHOULDER, current);
    }
    
    // DEBUG_PRINTLN("M26");
  }  
    

  else if (comm.check((char *)"G28"))
  {
    // Home
    this->homeArm();
    comm.send((char *)"HOMINGDONE");
  }
    

  else {
    comm.send((char *)"INVALID COMMAND");
  }
}

void robotArmControl::sendXYZ(void) {

  char buf[50] = {'\0'};
  char cBuf[10] = {'\0'};
  /*
  float rot = stepper.encoder.getAngleMoved();
  float left = this->bus.requestAngle(ELBOW);
  float right = this->bus.requestAngle(SHOULDER);

  DEBUG_PRINTLN("Motor angles: ");
  DEBUG_PRINT("BASE: ");
  DEBUG_PRINTLN(rot);
  DEBUG_PRINT("ELBOW: ");
  DEBUG_PRINTLN(left);
  DEBUG_PRINT("SHOULDER: ");
  DEBUG_PRINTLN(right);

  this->angleToxyz(rot, left, right, x, y, z);
  this->xyzToAngles(rot, left, right, x, y, z);*/
  // Build string
  strcpy(buf, "POS");
  strcat(buf, " X");
  strcat(buf, dtostrf(x, 0, 2, cBuf));
  strcat(buf, " Y");
  strcat(buf, dtostrf(y, 0, 2, cBuf));
  strcat(buf, " Z");
  strcat(buf, dtostrf(z, 0, 2, cBuf));
 //DEBUG_PRINTLN(buf);
  comm.send(buf);
}
/*To make the documentation useful we should rename or at least comment stuff so
 * that it fits with the document mogens made...*/
void robotArmControl::xyzToAngles(float &rot, float &left, float &right, float x, float y, float z) {

  /*DEBUG_PRINTLN("Position: ");
  DEBUG_PRINT("X: ");
  DEBUG_PRINTLN(x);
  DEBUG_PRINT("Y: ");
  DEBUG_PRINTLN(y);
  DEBUG_PRINT("Z: ");
  DEBUG_PRINTLN(z);*/

  rot = atan2(y, x);

  x -= cos(rot) * AXOFFSET;
  y -= sin(rot) * AXOFFSET;
  z -= AZOFFSET;

  float L1 = sqrt(
      (x * x) +
      (y * y)); // not offset in the x direction but in the L1 vector direction

  float L2 = sqrt(((L1 - XOFFSET) * (L1 - XOFFSET)) +
                  ((z - ZOFFSET) *
                   (z - ZOFFSET))); // again, it would be nice to make it fit
                                    // with the documentation. e.g. XOFFSET is
                                    // not an offset in the x direction...

  float a = (z - ZOFFSET) / L2;
  float b =
      ((L2 * L2) + (LOWERARMLEN * LOWERARMLEN) - (UPPERARMLEN * UPPERARMLEN)) /
      (2 * L2 * LOWERARMLEN);
  float c =
      ((LOWERARMLEN * LOWERARMLEN) + (UPPERARMLEN * UPPERARMLEN) - (L2 * L2)) /
      (2 * LOWERARMLEN * UPPERARMLEN);

  right = (atan2(a, sqrt(1 - (a * a))) + atan2(sqrt(1 - (b * b)), b));
  left = atan2(sqrt(1 - (c * c)), c);

  rot = (rot * RAD2DEGGEARED) - ROTOFFSET; // This is Theta1

  left -= ELBOWOFFSET;

  right -= SHOULDEROFFSET;

  left = ((left + right) *
           RAD2DEGGEARED); // Remeber to map elbow angle down to motor angle
  right *= RAD2DEGGEARED;
/*
  DEBUG_PRINTLN("Motor angles: ");
  DEBUG_PRINT("BASE: ");
  DEBUG_PRINTLN(rot);
  DEBUG_PRINT("ELBOW: ");
  DEBUG_PRINTLN(left);
  DEBUG_PRINT("SHOULDER: ");
  DEBUG_PRINTLN(right);*/

}

void robotArmControl::angleToxyz(float rot, float left, float right,
                                 float &x_actual, float &y_actual,
                                 float &z_actual) {
  // From the documentation the elbow angle theta3 i s the manipulated through
  // the secondary gear The primary gear i s manipulat ing the shoulder angle
  // theta2
/*
  DEBUG_PRINTLN("Motor angles: ");
  DEBUG_PRINT("BASE: ");
  DEBUG_PRINT(rot);
  DEBUG_PRINT("  ELBOW: ");
  DEBUG_PRINT(left);
  DEBUG_PRINT("  SHOULDER: ");
  DEBUG_PRINTLN(right);
*/
  // REMEMBER TO ADD OFFSET TO ACTUATOR!
  rot = (DEG2RADGEARED * rot) + ROTOFFSET;

  right *= DEG2RADGEARED;
  left *= DEG2RADGEARED;

  left = (left - right) + ELBOWOFFSET; // Remember to map secondary gear angle to elbow angle !
  right += SHOULDEROFFSET;

  z = ZOFFSET + sin(right) * LOWERARMLEN -
      cos(left - (MATH_PI_HALF - right)) * UPPERARMLEN +
      AZOFFSET; // o f f s e t in the Z di r e c ton f o r the a c tua tor i s
                // added here

  float k1 = sin(left - (MATH_PI_HALF - right)) * UPPERARMLEN +
             cos(right) * LOWERARMLEN + XOFFSET +
             AXOFFSET; // o f f s e t in the L1 di r e c ton f o r the a c tua
                       // tor i s added here

  x = cos(rot) * k1;
  y = sin(rot) * k1;
/*
  DEBUG_PRINTLN("Position: ");
  DEBUG_PRINT("X: ");
  DEBUG_PRINT(x);
  DEBUG_PRINT("  Y: ");
  DEBUG_PRINT(y);
  DEBUG_PRINT("  Z: ");
  DEBUG_PRINTLN(z);
  */
}

// Function is only used by master to control all three motors
void robotArmControl::setMotorAngle(uint8_t num, float angle) {

  if (num == BASE) {

    stepper.moveToAngle(this->direction * angle);

  } else {

    bus.setAngle(num, angle);
  }
}

// Function is only used by master to control all three motors
void robotArmControl::setMotorSpeed(uint8_t num, float speed) {

  if (num == BASE) {
    stepper.setMaxVelocity(speed);

  } else {

    bus.setSpeed(num, speed);
  }
}

void robotArmControl::setMotorHomingSpeed(uint8_t num, float speed)
{
  if (num == BASE) {
    this->homeSpeed = speed;
  } else {
    bus.setHomingSpeed(num, speed);
  }
}

void robotArmControl::setMotorCurrent(uint8_t num, float current)
{
  if (num == BASE) {
    stepper.setCurrent(current);
  } else {
    bus.setCurrent(num, current);
  }
}

void robotArmControl::setMotorHoldCurrent(uint8_t num, float current)
{
  if (num == BASE) {
    stepper.setHoldCurrent(current);
  } else {
    bus.setHoldCurrent(num, current);
  }
}

void robotArmControl::setMotorStallSense(uint8_t num, float sense)
{
  if (num == BASE) {
    this->stallSense = (int8_t)sense;
  } else {
    bus.setStallSense(num, sense);
  }
}

// Function is only used by master to control all three motors
void robotArmControl::setMotorAcceleration(uint8_t num, float acceleration) {
  DEBUG_PRINTLN(acceleration);
  if (num == BASE) {
    stepper.setMaxAcceleration(acceleration);
    stepper.setMaxDeceleration(acceleration);
  } else {

    bus.setAcceleration(num, acceleration);
  }
}

// Function is only used by master to control all three motors
void robotArmControl::setbrakeMode(uint8_t num, float brakeMode) {

  if (num == BASE) {
    if(brakeMode == 0.0)
    {
      stepper.setBrakeMode(FREEWHEELBRAKE);
    }
    else if(brakeMode == 1.0)
    {
      stepper.setBrakeMode(COOLBRAKE);
    }
    else if(brakeMode == 2.0)
    {
      stepper.setBrakeMode(HARDBRAKE);
    }
  } else {

    bus.setBrakeMode(num, brakeMode);
  }
}

// Function is only used by master to control all three motors
void robotArmControl::runContinously(uint8_t num, float speed) {
  float velocity = this->direction * speed;

  if (num == BASE) {
    stepper.setMaxVelocity(velocity);

    if (velocity < 0.0) {
      stepper.runContinous(CCW);
    } else {
      stepper.runContinous(CW);
    }
  } else {

    bus.runContinously(num, speed);
  }
}

// Function for checking the constraints on the robot before moving - we can
// decide to either move to closest possible or return fault
uint8_t robotArmControl::checkLimits(bool correction) {
  uint8_t jointsAllowedToMove = 0xFF;//0 = not allowed to move, 1 allowed to move. bit0 = base, bit1 = shoulder, bit2 = elbow

  if(this->angleBase < (-160.0 * GEARRATIO) && this->targetBaseSpeed < 0)
  {
    jointsAllowedToMove &= ~0x01;  //not ok
  }
  else if(this->angleBase > (160.0 * GEARRATIO) && this->targetBaseSpeed > 0)
  {
    jointsAllowedToMove &= ~0x01;  //not ok
  }

  /********Shoulder hard limits**********/
  if(this->angleShoulder < (-120.0 * GEARRATIO) && this->targetShoulderSpeed > 0)
  {
    jointsAllowedToMove &= ~0x02;  //not ok
  }
  else if(this->angleShoulder > (-2.0 * GEARRATIO) && this->targetShoulderSpeed < 0)
  {
    jointsAllowedToMove &= ~0x02;  //not ok
  }

  /***********Elbow hard limits*************/
  if(this->angleElbow < (-90.0 * GEARRATIO) && this->targetElbowSpeed > 0)
  {
    jointsAllowedToMove &= ~0x04;  //not ok
  }
  else if(this->angleElbow > (-2.0 * GEARRATIO) && this->targetElbowSpeed < 0)
  {
    jointsAllowedToMove &= ~0x04;  //not ok
  }
  /**************Elbow/shoulder dynamic limits**********************/
  if(this->angleElbow - this->angleShoulder < (5.0 * GEARRATIO))
  {
    if(this->targetElbowSpeed < 0 && this->targetShoulderSpeed > 0)
    {
      //intentionally left blank 
    }
    else if(this->targetElbowSpeed < 0)
    {
      jointsAllowedToMove &= ~0x02; // shoulder not ok to move
    }
    else
    {
      jointsAllowedToMove &= ~0x04; // elbow not ok to move
    }
  }
  if(correction == false)
  {
    if(this->sy == 0.0)// Due to rounding errors the y-axis can twitch even though not commanded to move
    {
      DEBUG_PRINTLN("MASTER STOPPED");
      jointsAllowedToMove &= ~0x01;// so dont move if not commanded to
    }
  }
  
  return jointsAllowedToMove;
}

void robotArmControl::setPump(bool state) {
  DEBUG_PRINTLN("PUMPE");
  
  if(state)
  {
    DDRD |= (1 << 2);
    PORTD |= (1 << 2);
  }
  else
  {
    DDRD &= ~(1 << 2);
    PORTD &= ~(1 << 2);
    DDRD |= (1 << 3);
    this->valveOn = 1;
    this->valveOnTime = millis() + 5000;
  }
  this->currentPumpState = state;
}

void robotArmControl::busReceiveEvent(void) {

  binaryFloat value;

  char buf[5] = {'\0'};
  uint8_t i = 0;
  char c;

  // read incoming data from master
  while (I2CPORT.available()) {
    if (i < 5) {
      c = I2CPORT.read();
      buf[i++] = c;
    }
  }

  if (i == 5) {
    switch (buf[0]) {
    case 'r': // Reset home
      nextCommand = resetHome;
      break;
    case 'h': // Go to home
      nextCommand = home;
      break;
    case 'B': // Stop
      nextCommand = stop;
      break;
    case 'i': // set acceleration
      value.b[0] = buf[1];
      value.b[1] = buf[2];
      value.b[2] = buf[3];
      value.b[3] = buf[4];
      nextCommandArgument.f = value.f;
      nextCommand = setAcceleration;
      break;
    case 's': // set speed
      value.b[0] = buf[1];
      value.b[1] = buf[2];
      value.b[2] = buf[3];
      value.b[3] = buf[4];
      nextCommandArgument.f = value.f;
      nextCommand = setVelocity;
      break;
    case 'S': // runContinously
      value.b[0] = buf[1];
      value.b[1] = buf[2];
      value.b[2] = buf[3];
      value.b[3] = buf[4];
      nextCommandArgument.f = value.f;
      nextCommand = runContinouslyCmd;
      break;
    case 'a': // Move to angle
    {
      value.b[0] = buf[1];
      value.b[1] = buf[2];
      value.b[2] = buf[3];
      value.b[3] = buf[4];

      nextCommandArgument.f = value.f;
      nextCommand = move;
    } break;
    case 'b': // setBrakeMode
    {
      value.b[0] = buf[1];
      value.b[1] = buf[2];
      value.b[2] = buf[3];
      value.b[3] = buf[4];

      nextCommandArgument.f = value.f;
      nextCommand = setBrakeMode;
    } break;
    case 'H': // setHomeSpeed
    {
      value.b[0] = buf[1];
      value.b[1] = buf[2];
      value.b[2] = buf[3];
      value.b[3] = buf[4];

      nextCommandArgument.f = value.f;
      nextCommand = setHomeSpeed;
    } break;
    case 'f': // setSensitivity
    {
      value.b[0] = buf[1];
      value.b[1] = buf[2];
      value.b[2] = buf[3];
      value.b[3] = buf[4];

      nextCommandArgument.f = value.f;
      nextCommand = setStallSense;
    } break;
    case 'c': // setCurrent
    {
      value.b[0] = buf[1];
      value.b[1] = buf[2];
      value.b[2] = buf[3];
      value.b[3] = buf[4];

      nextCommandArgument.f = value.f;
      nextCommand = setCurrent;
    } break;
    case 'C': // setHoldCurrent
    {
      value.b[0] = buf[1];
      value.b[1] = buf[2];
      value.b[2] = buf[3];
      value.b[3] = buf[4];

      nextCommandArgument.f = value.f;
      nextCommand = setHoldCurrent;
    } break;
    case 'q': // request some information. does not affect state
      value.b[0] = buf[1];
      value.b[1] = buf[2];
      value.b[2] = buf[3];
      value.b[3] = buf[4];
      address = value.f;
      break;
    };
  }
}

void robotArmControl::busRequestEvent(void) {
  // Create union to store float
  binaryFloat value;
  uint8_t data;

  if (address == 0.0) {
    value.f = ptr->direction * stepper.angleMoved();
    // Send each byte of the float through the union
    I2CPORT.write(value.b, 4);
  } else if (address == 1.0) {
    I2CPORT.write((uint8_t *)&state, 1);
    address = 0.0;
  } else if (address == 2.0) {
    data = stepper.getMotorState(STANDSTILL);
    I2CPORT.write(&data, 1);
    address = 0.0;
  }
}
