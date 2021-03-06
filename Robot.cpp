#include "Robot.h"
#include "WPILib.h"
#include <opencv2/core/core.hpp> //TODO: do we need this?

static float scaleJoysticks(float power, float dead, float max, int degree) {
	if (degree < 0) {	// make sure degree is positive
		degree = 1;
	}
	if (degree % 2 == 0) {	// make sure degree is odd
		degree++;
	}
	if (fabs(power) < dead) {	// if joystick input is in dead zone, return 0
		return 0;
	}
	else if  (power > 0) {	// if it is outside of the dead zone, then the output is a function of specified degree centered at the end of the dead zone
		return (max * pow(power - dead, degree) / pow(1 - dead, degree));
	}
	else {
		return (max * pow(power + dead, degree) / pow(1 - dead, degree));
	}
}

Robot::Robot() :
		frontLeftMotor(Constants::frontLeftDriveChannel),
		rearLeftMotor(Constants::rearLeftDriveChannel),
		frontRightMotor(Constants::frontRightDriveChannel),
		rearRightMotor(Constants::rearRightDriveChannel),
		robotDrive(frontLeftMotor, rearLeftMotor, frontRightMotor, rearRightMotor),
		driveStick(Constants::driveStickChannel),
		operatorStick(Constants::operatorStickChannel),
		gyro(I2C::Port::kMXP, 200),
		pid(),
		aimer(),
		leftProx(1, 0),
		rightProx(3, 2),
		leftIR(5),
		rightIR(4),
		gear(Constants::gearActuatorInSole, Constants::gearActuatorOutSole),
		shooter(Constants::rotatorChannel, Constants::shooterChannel, Constants::agitatorChannel),
		compressor(),
		filter(),
		intake(Constants::intakeMotorPin, Constants::verticalConveyorMotorPin),
		climber(Constants::climberPin),
		brakes(Constants::brakesInSole, Constants::brakesOutSole),
		pdp(),
		encoder(NULL) //TODO: make sure we need this
		arduino(I2C::kOnboard, 6)
{
	encoder = new Encoder(8,9,false,Encoder::EncodingType::k4X);
	encoder->SetDistancePerPulse(.0092);
	robotDrive.SetExpiration(0.1);
	gyro.ZeroYaw();
	robotDrive.SetInvertedMotor(RobotDrive::kFrontLeftMotor, false);
	robotDrive.SetInvertedMotor(RobotDrive::kRearLeftMotor, false);
	robotDrive.SetInvertedMotor(RobotDrive::kFrontRightMotor, true);
	robotDrive.SetInvertedMotor(RobotDrive::kRearRightMotor, true);
}

void Robot::RobotInit() {
	camera0 =  CameraServer::GetInstance()->StartAutomaticCapture();
	camera0.SetResolution(640, 480);
	camera0.SetExposureManual(78);
//	camera1 = CameraServer::GetInstance()->StartAutomaticCapture(1);
//	camera1.SetResolution(640, 480);
//	camera1.SetExposureManual(312);
	NetworkTable::SetUpdateRate(.01);
	SmartDashboard::PutNumber("Joe P-value", -(Constants::accumulatorPower));
	SmartDashboard::PutNumber("shooterSpeed", .9);
	float ultrasonicFrontRight = 0;
	float ultrasonicFrontLeft = 0;
	float ultrasonicCenterRight = 0;
	float ultrasonicCenterLeft = 0;
	float ultrasonicBackRight = 0;
	float ultrasonicBackLeft = 0;
	uint8_t toSend[10];//array of bytes to send over I2C
	uint8_t toReceive[50];//array of bytes to receive over I2C
	uint8_t numToSend = 2;//number of bytes to send
	uint8_t numToReceive = 28;//number of bytes to receive

	uint8_t lightcannonFront = 0;
	uint8_t lightcannonBack = 0;

	SmartDashboard::PutNumber("ultraFR", ultrasonicFrontRight);
	SmartDashboard::PutNumber("ultraFL", ultrasonicFrontLeft);
	SmartDashboard::PutNumber("ultraCR", ultrasonicCenterRight);
	SmartDashboard::PutNumber("ultraCL", ultrasonicCenterLeft);
	SmartDashboard::PutNumber("ultraBR", ultrasonicBackRight);
	SmartDashboard::PutNumber("ultraBL", ultrasonicBackLeft);

}

/**
 * Runs the motors with Mecanum drive.
 */

inline float getAverageDistance(const Ultrasonic& leftProx, const Ultrasonic& rightProx);

void Robot::OperatorControl()
{
	robotDrive.SetSafetyEnabled(false);
	pid.setAngle(SmartDashboard::GetNumber("angle_p", Constants::angle_p_default), SmartDashboard::GetNumber("angle_i", Constants::angle_i_default), SmartDashboard::GetNumber("angle_d", Constants::angle_d_default));
	pid.setY(SmartDashboard::GetNumber("y_p", Constants::y_p_default), SmartDashboard::GetNumber("y_i", Constants::y_i_default), SmartDashboard::GetNumber("y_d", Constants::y_d_default));
	pid.setX(SmartDashboard::GetNumber("x_p", Constants::x_p_default), SmartDashboard::GetNumber("x_i", Constants::x_i_default), SmartDashboard::GetNumber("x_d", Constants::x_d_default));
	gyro.ZeroYaw();
	gyro.Reset();
	gyro.ResetDisplacement();

	float driveX;
	float driveY;
	float driveZ;
	float angle;
	float angleOutput = 0; //pid loop output
	float gearAngle = 0;
	float yOutput;
	float xOutput;

	float voltage = 0; //testing data for battery voltage
	bool gyroValid;
	bool calibrating;

	//auto move
	bool inAutoMoveLoop = false; //to force quit field oriented drive when in an auto move loop - auto move only works in robot oriented drive

	//shooter
	bool shooterAutoAngleButtonPressed = false;
	bool shooterShootButtonPressed = false;
	bool shooting = false;
	float shooterSpeed = 0.0;
	bool shooterAngleReached = true;

	//gear
	bool gearButtonPressed = false;
	Timer gearOpenTimer;
	gearOpenTimer.Start();

	//one axis
	bool oneAxisButtonPressed = false;
	float oneAxisDesiredAngle = 0.0;

	//ultrasonics
	float leftUltrasonic = leftProx.GetRangeInches();
	float rightUltrasonic = rightProx.GetRangeInches();

	//field oriented driveZAxis
	bool fieldOrientedDrive = false;
	bool fieldOrientedDriveButtonPressed = false;

	//intake
	bool intakeButtonPressed = false;
	bool intakeRunning = true;
	float intakeCurrent = 0.0;

	//climber
	//bool climberButtonPressed = false;
	//bool climbing = false;

	//brakes
	//bool brakeButtonPressed = false;

	//flip drive orientation
	int flipDriveOrientation = 1;

	//init functions
	//filter.initializeLastUltrasonics(leftUltrasonic, rightUltrasonic);
	//filter.initializePredictedValue(leftUltrasonic, rightUltrasonic);
	leftProx.SetAutomaticMode(true);
	rightProx.SetAutomaticMode(true);
	shooter.enable();
	climber.enable();
	compressor.Start();

	//accumulator
	Accumulator accum((float)0.0, (float)0.0, (float)0.0, 2, leftProx, rightProx, aimer, *encoder, pid);
	double lastLeftProxValue = 0;
	double lastLeftProxTime = 0;
	double lastRightProxValue = 0;
	double lastrightProxTime = 0;
	double lastEncoderDistance = 0;
	DoubleDouble driveVals(0, 0, 0);
	double yTargetDistance = -9999.0;
	bool inYTargetMode = false;

	//camera switching
	int cameraInOperation = 1;
	bool cameraSwitching = false;
	bool cameraSwapButtonPressed = false;

	while (IsOperatorControl() && IsEnabled())
	{
		shooterSpeed = SmartDashboard::GetNumber("shooterSpeed", .9);
		/*
		 *
		 * BASIC CHECKS
		 *
		 */

		 gyroValid = gyro.IsConnected();
		 SmartDashboard::PutBoolean("gyro alive", gyroValid);
		 calibrating = gyro.IsCalibrating();
		 voltage = DriverStation::GetInstance().GetBatteryVoltage();
		 angleOutput = 0; //reset output so that if the pid loop isn't being called it's not reserved from the last time it's called
		 angle = gyro.GetYaw() < 0 ? 360 + gyro.GetYaw() : gyro.GetYaw(); //TODO: swap front back
		 yOutput = 0; //reset output
		 xOutput = 0;
		 //new ultrasonic
		 lightcannonFront = 20;
		lightcannonBack = 20;
		toSend[0] = lightcannonFront;
		toSend[1] = lightcannonBack;
		arduino.Transaction(toSend, numToSend, toReceive, numToReceive);
		ultrasonicFrontRight[0] = toReceive[4];
		ultrasonicFrontRight[1] = toReceive[5];
		ultrasonicFrontRight[2] = toReceive[6];
		ultrasonicFrontRight[3] = toReceive[7];
		ultrasonicFrontLeft[0] = toReceive[8];
		ultrasonicFrontLeft[1] = toReceive[9];
		ultrasonicFrontLeft[2] = toReceive[10];
		ultrasonicFrontLeft[3] = toReceive[11];
		ultrasonicCenterRight[0] = toReceive[12];
		ultrasonicCenterRight[1] = toReceive[13];
		ultrasonicCenterRight[2] = toReceive[14];
		ultrasonicCenterRight[3] = toReceive[15];
		ultrasonicCenterLeft[0] = toReceive[16];
		ultrasonicCenterLeft[1] = toReceive[17];
		ultrasonicCenterLeft[2] = toReceive[18];
		ultrasonicCenterLeft[3] = toReceive[19];
		ultrasonicBackRight[0] = toReceive[20];
		ultrasonicBackRight[1] = toReceive[21];
		ultrasonicBackRight[2] = toReceive[22];
		ultrasonicBackRight[3] = toReceive[23];
		ultrasonicBackLeft[0] = toReceive[24];
		ultrasonicBackLeft[1] = toReceive[25];
		ultrasonicBackLeft[2] = toReceive[26];
		ultrasonicBackLeft[3] = toReceive[27];
		SmartDashboard::PutNumber("ultraFR", ultrasonicFrontRight);
		SmartDashboard::PutNumber("ultraFL", ultrasonicFrontLeft);
		SmartDashboard::PutNumber("ultraCR", ultrasonicCenterRight);
		SmartDashboard::PutNumber("ultraCL", ultrasonicCenterLeft);
		SmartDashboard::PutNumber("ultraBR", ultrasonicBackRight);
		SmartDashboard::PutNumber("ultraBL", ultrasonicBackLeft);
		//old ultrasonic 
		 leftUltrasonic = leftProx.GetRangeInches();
		 rightUltrasonic = rightProx.GetRangeInches();
		 /*if (driveStick.GetRawButton(Constants::cancelAllButton)) {
			 shooterAutoAngleButtonPressed = false;
			 shooterShootButtonPressed = false;
			 shooting = false;
			 shooterSpeed = 0.0;
			 shooterAngleReached = true;
			 intakeButtonPressed = false;
			 intakeRunning = false;
			 climberButtonPressed = false;
			 climbing = false;
			 gearButtonPressed = true;
			 gearOpenCounter = 1001;
			 gear.setBottom(false); //bottom closed - TODO: may need to flip
			 shooter.stop();
			 climber.setSpeed(0.0);
			 intake.runIntake(0.0);
			 intake.runVerticalConveyor(0.0);
		 }*/ //TODO: uncomment

		if(driveStick.GetRawButton(12)){
			gyro.ZeroYaw();
		}

		/*
		 *
		 * END BASIC CHECKS
		 *
		 */

		/*
		 *
		 * SHOOTER CODE
		 *
		 */

		if (driveStick.GetRawButton(Constants::shooterAutoAngleButton) && !shooterAutoAngleButtonPressed && !shooterAngleReached) { //if the shooter has been started and the button was let go and then pressed
			shooterAngleReached = true; //cancel shooter auto aim
		}
		if (driveStick.GetRawButton(Constants::shooterAutoAngleButton) && !shooterAutoAngleButtonPressed && shooterAngleReached)  { //if the shooter has not been started and the button was let go and then pressed
			shooterAngleReached = shooter.setAngle(aimer.GetAngleToShoot()); //angle from tj's vision code
			shooterAutoAngleButtonPressed = true; //set button pressed to true so that holding the button for more than 10ms (loop time) doesn't activate the loop above and cancel it
		}
		if (!driveStick.GetRawButton(Constants::shooterAutoAngleButton)) { //if the shooter button has been let go
			shooterAutoAngleButtonPressed = false; //set to false so it can be pressed again
		}
		if (!shooterAngleReached) { //if the shooter angle hasn't yet been reached
			shooterAngleReached = shooter.setAngle(aimer.GetAngleToShoot()); //still shooting
		}
		if (driveStick.GetRawButton(Constants::shooterShootButton) && !shooterShootButtonPressed && !shooting) { //shoot
			//TODO: get shooter speed
			shooter.shoot(shooterSpeed); //shoot at the speed
			shooterShootButtonPressed = true; //button is pressed so it doesn't immediately cancel
			shooting = true; //set shooting to true so it knows next time the button is pressed to cancel
		} else if (driveStick.GetRawButton(Constants::shooterShootButton) && !shooterShootButtonPressed && shooting) { //cancel
			shooter.stop(); //turn off shooter
			shooterShootButtonPressed = true; //so it doesn't immediately start shooting again
			shooting = false; //so it knows what loop to go into
		}
		if (shooting) {
			shooter.shoot(shooterSpeed);
		}
		if (!driveStick.GetRawButton(Constants::shooterShootButton)) { //when button is let go
			shooterShootButtonPressed = false; //let shooter change state
		}

		shooter.move(operatorStick.GetRawAxis(1)); //TODO: delete after testing

		/*
		 *
		 * END SHOOTER CODE
		 *
		 */



		/*
		 *
		 * GEAR CODE
		 *
		 */

		if(driveStick.GetRawButton(Constants::gearActuateButton) && !gearButtonPressed) { //open / close gear
			gear.setBottom(!gear.getBottom()); //set to the opposite of what it currently is
			gearButtonPressed = true; //button still pressed = true
		} else if (!driveStick.GetRawButton(Constants::gearActuateButton)) { //button not pressed - so it doesn't keep flipping between open and closed
			gearButtonPressed = false; //button not pressed
		}
		if (!gear.getBottom()) { //TODO: may need to make a not
			gearOpenTimer.Reset(); //reset timer to zero so it doesn't open after 5 seconds
		}
		if (gearOpenTimer.Get() > 5) { //if it's been open for more than 5 seconds
			gear.setBottom(!gear.getBottom()); //set to closed
			gearOpenTimer.Reset();
		}

		/*
		 *
		 * END GEAR CODE
		 *
		 */

		/*
		 *
		 * INTAKE CODE
		 *
		 */

		intakeCurrent = pdp.GetCurrent(Constants::intakePDPChannel);
		if (driveStick.GetRawButton(Constants::intakeActivateButton) && !intakeButtonPressed) {
			intakeRunning = !intakeRunning; //if on, turn off. If off, turn on
			intakeButtonPressed = true; //doesn't keep flipping when held down
		} else if (!driveStick.GetRawButton(Constants::intakeActivateButton)) {
			intakeButtonPressed = false; //lets you flip the state again
		}
		if (intakeRunning) { //if it's turned on
			intake.runIntake(Constants::intakeRunSpeed); //run intake
			intake.runVerticalConveyor(Constants::verticalConveyorRunSpeed); //run vertical conveyor
		} else { //if it's turned off
			intake.runIntake(0.0); //stop intake
			intake.runVerticalConveyor(0.0); //stop vertical conveyor
		}
		if (intakeCurrent > Constants::intakeCurrentMax) { //if the current is too high, turn off - SAFETY
			intake.runIntake(0.0); //off
			intake.runVerticalConveyor(0.0); //off
		}

		/*
		 *
		 * END INTAKE CODE
		 *
		 */



		/*
		 *
		 * CLIMBER CODE
		 *
		 */

		/*if (driveStick.GetRawButton(Constants::climbButton) && !climberButtonPressed) {
			climbing = !climbing; //flip state
			climberButtonPressed = true; //don't keep flipping states when held down
		} else if (!driveStick.GetRawButton(Constants::climbButton)) { //if not pressed anymore
			climberButtonPressed = false; //lets you flip state again
		}
		if (climbing) { //if it's running
			climber.setSpeed(Constants::climberRunSpeed); //run
		} else { //if it's not running
			climber.setSpeed(0.0); //stop
		}*/

		if (driveStick.GetRawButton(1)) {
			climber.setSpeed(-1.0);
		} else if (driveStick.GetRawButton(12)) {
			climber.setSpeed(1.0);
		} else {
			climber.setSpeed(0.0);
		}

		/*
		 *
		 * END CLIMBER CODE
		 */

		/*
		 *
		 * BRAKE CODE
		 *
		 */

		/*if (driveStick.GetRawButton(Constants::brakeButton) && !brakeButtonPressed) { //if activating brakes
			brakes.set(!brakes.get()); //flip state
			brakeButtonPressed = true; //don't flip state when held down
		} else if (!driveStick.GetRawButton(Constants::brakeButton)) { //if not pressed anymore
			brakeButtonPressed = false; //lets you flip state again
		}*/ //BRAKES ARE OFF AT THE MOMENT

		/*
		 *
		 * END BRAKE CODE
		 *
		 */

		/*
		 *
		 * FLIP DRIVE ORIENTATION CODE
		 *
		 */

		if (driveStick.GetRawButton(Constants::swapDriveButton)) {
			flipDriveOrientation = -1;
		} else {
			flipDriveOrientation = 1;
		}

		/*
		 *
		 * PID CODE
		 *
		 */

		if(driveStick.GetPOV() != -1 && gyroValid) { //turn to angle 0, 90, 180, 270
			angleOutput = pid.PIDAngle(angle, driveStick.GetPOV()); //call pid loop
			inAutoMoveLoop = true; //auto moving - force quit field oriented drive
		} else {
			pid.resetPIDAngle(); //if loop is done reset values
			inAutoMoveLoop = false; //reopen field oriented drive (if active)
		}

		if(driveStick.GetRawButton(Constants::moveToGearButton)) { //pid move
			angleOutput = pid.PIDAngle(angle, 0); //TODO: get angle via function (closest angle of the 3 options (0, 60, -60, find which is closest to current angle)?)
			if (angleOutput == 0) {
				xOutput = filter.ultrasonicFilter(leftUltrasonic, rightUltrasonic) > 45 ? pid.PIDX(aimer.TwoCameraAngleFilter()) : 0.0; //if done turning move x
				yOutput = (xOutput < .01) ? pid.PIDY(leftUltrasonic, rightUltrasonic) : 0.0; //if done moving x move y
			}
			inAutoMoveLoop = true; //force quit field oriented drive
		} else { //if loop is done reset values
			pid.resetPIDX();
			pid.resetPIDY();
			inAutoMoveLoop = false; //reopen field oriented drive
		}

		/*
		 *
		 * END PID CODE
		 *
		 */

		/*
		 *
		 * DRIVE CODE
		 *
		 */

		//scaling for the joystick deadzones - TODO: fix scaling for the ps4 controllers
		driveX = driveStick.GetRawAxis(Constants::driveXAxis);
		driveY = driveStick.GetRawAxis(Constants::driveYAxis);
		driveZ = driveStick.GetRawAxis(Constants::driveZAxis);

		driveX = scaleJoysticks(driveX, Constants::driveXDeadZone, Constants::driveXMax * (.5 - (driveStick.GetRawAxis(Constants::driveThrottleAxis) / 2)), Constants::driveXDegree);
		driveY = scaleJoysticks(driveY, Constants::driveYDeadZone, Constants::driveYMax * (.5 - (driveStick.GetRawAxis(Constants::driveThrottleAxis) / 2)), Constants::driveYDegree);
		driveZ = scaleJoysticks(driveZ, Constants::driveZDeadZone, Constants::driveZMax * (.5 - (driveStick.GetRawAxis(Constants::driveThrottleAxis) / 2)), Constants::driveZDegree);

		if (driveStick.GetRawButton(Constants::driveOneAxisButton)) { //drive only one axis
			if (!oneAxisButtonPressed) { //if not in the middle of being held down
				oneAxisButtonPressed = true; //being held down now - don't reset desired angle
				oneAxisDesiredAngle = angle; //set desired angle
			}
			if (fabs(driveX) > fabs(driveY) && fabs(driveX) > fabs(driveZ)) { //if X is greater than Y and Z, then it will only go in the direction of X
				angleOutput = pid.PIDAngle(angle, oneAxisDesiredAngle); //stay straight
				driveY = 0;
				driveZ = 0;
			}
			else if (fabs(driveY) > fabs(driveX) && fabs(driveY) > fabs(driveZ)) { //if Y is greater than X and Z, then it will only go in the direction of Y
				angleOutput = pid.PIDAngle(angle, oneAxisDesiredAngle); //stay straight
				driveX = 0;
				driveZ = 0;
			}
			else { //if Z is greater than X and Y, then it will only go in the direction of Z
				driveX = 0;
				driveY = 0;
			}
		} else {
			oneAxisButtonPressed = false; //lets you reset the straight facing angle when you let go of the button
		}

		driveX = fabs(driveX + xOutput) > 1 ? std::copysign(1, driveX + xOutput) : driveX + xOutput; //if driving and pid'ing (pls dont) maxes you out at 1 so the motors move
		driveY = fabs(driveY + yOutput) > 1 ? std::copysign(1, driveY + yOutput) : driveY + yOutput; //if driving and pid'ing (pls dont) maxes you out at 1 so the motors move
		driveZ = fabs(driveZ + angleOutput) > 1 ? std::copysign(1, driveZ + angleOutput) : driveZ + angleOutput; //if driving and pid'ing (pls dont) maxes you out at 1 so the motors move

		driveX *= flipDriveOrientation; //flip front / back rotation
		driveY *= flipDriveOrientation;

		if (driveStick.GetRawButton(Constants::fieldOrientedDriveButton) && !fieldOrientedDriveButtonPressed) {
			fieldOrientedDrive = !fieldOrientedDrive;
			fieldOrientedDriveButtonPressed = true;
		} else if (!driveStick.GetRawButton(Constants::fieldOrientedDriveButton)) {
			fieldOrientedDriveButtonPressed = false;
		}

		if (fieldOrientedDrive && !inAutoMoveLoop) {
			robotDrive.MecanumDrive_Cartesian(driveX, driveY, driveZ, angle); //field oriented drive
		} else {
			robotDrive.MecanumDrive_Cartesian(driveX, driveY, driveZ); //robot oriented drive
		}

		/*
		 *
		 * END DRIVE CODE
		 *
		 */

		/*
		 *
		 * CAMERA SWAP CODE
		 *
		 */


		if (driveStick.GetRawButton(9) && !cameraSwapButtonPressed) {
			cameraInOperation++;
			cameraInOperation = cameraInOperation % 2;
			SmartDashboard::PutNumber("camera in operation", cameraInOperation);
			if (cameraInOperation == 0) {
				CameraServer::GetInstance()->PutVideo(camera0.GetName(), 640, 480);
			} else {
				CameraServer::GetInstance()->PutVideo(camera1.GetName(), 640, 480);

			}
			cameraSwapButtonPressed = true;
		} else if (!driveStick.GetRawButton(9)) {
			cameraSwapButtonPressed = false;
		}

		/*
		 *
		 * END CAMERA SWAP CODE
		 *
		 */


		/*
		 *
		 * JOSEPH MOVE TEST CODE
		 *
		 */

		if(driveStick.GetRawButton(5))
		{
			driveVals = accum.drive(inYTargetMode, true, false, false, 12, 0, 2, angle);
			driveX = driveVals.x;
			driveY = driveVals.y;
			driveZ = driveVals.angle;
			inYTargetMode = true;
		}
		else
		{
			SmartDashboard::PutBoolean("Joe in loop", false);
			inYTargetMode = false;
		}



		SmartDashboard::PutNumber("Joseph driveX", driveX);
		SmartDashboard::PutNumber("Joseph driveY", driveY);
		SmartDashboard::PutNumber("Joseph driveZ", driveZ);
		robotDrive.MecanumDrive_Cartesian(driveX, driveY, driveZ); //drive

		/*
		 *
		 * END JOSEPH MOVE TEST CODE
		 *
		 */

		/*
		 *
		 * SMART DASHBOARD
		 *
		 */

		SmartDashboard::PutNumber("leftProx", leftUltrasonic);
		SmartDashboard::PutNumber("rightProx", rightUltrasonic);
		SmartDashboard::PutBoolean("leftIR", leftIR.get());
		SmartDashboard::PutBoolean("rightIR", rightIR.get());
		SmartDashboard::PutNumber("angleOutput", angleOutput);
		SmartDashboard::PutNumber("Angle", angle);
		SmartDashboard::PutBoolean("Is Rotating", gyro.IsRotating());
		SmartDashboard::PutNumber("Requested Update rate", gyro.GetRequestedUpdateRate());
		SmartDashboard::PutNumber("Actual Update rate", gyro.GetActualUpdateRate());
		SmartDashboard::PutNumber("getPOV", driveStick.GetPOV());
		SmartDashboard::PutNumber("GearAngleCalculated", gearAngle);
		SmartDashboard::PutNumber("TwoCameraAngleFilter", aimer.TwoCameraAngleFilter());
		SmartDashboard::PutNumber("leftAngleToGear", aimer.GetLeftAngleToGear());
		SmartDashboard::PutNumber("rightAngleToGear", aimer.GetRightAngleToGear());
		SmartDashboard::PutNumber("yOutput", yOutput);
		SmartDashboard::PutNumber("xOutput", xOutput);
		SmartDashboard::PutNumber("Encoder value", encoder->GetDistance());
//		SmartDashboard::PutNumberArray("leftAngleArray", aimer.GetLeftAngleArray());
//		SmartDashboard::PutNumberArray("rightAngleArray", aimer.GetRightAngleArray());
		SmartDashboard::PutBoolean("calibrateButtonPushed", calibrating);
		SmartDashboard::PutNumber("voltage", voltage);
		//SmartDashboard::PutNumber("Ultrasonic Filter", filter.ultrasonicFilter(leftUltrasonic, rightUltrasonic));
		//SmartDashboard::PutNumber("AngleToGear", aimer.GetAngleToGear());
		SmartDashboard::PutNumber("xDisp", gyro.GetDisplacementX());
		SmartDashboard::PutNumber("yDisp", gyro.GetDisplacementY());
		SmartDashboard::PutNumber("xVel", gyro.GetVelocityX());
		SmartDashboard::PutNumber("yVel", gyro.GetVelocityY());
		SmartDashboard::PutNumber("xAccel", gyro.GetRawAccelX());
		SmartDashboard::PutNumber("yAccel", gyro.GetRawAccelY());
		SmartDashboard::PutBoolean("IsCalibrating", gyro.IsCalibrating());
		SmartDashboard::PutNumber("Ultrasonic Kalman Filter", filter.kalmanFilter(leftUltrasonic, rightUltrasonic, driveY));
		SmartDashboard::PutBoolean("Braking", brakes.get());
		SmartDashboard::PutBoolean("Gear State", gear.getBottom());
		SmartDashboard::PutNumber("Intake Current", intakeCurrent);
		SmartDashboard::PutNumber("shooter pitch", shooter.Pitch());
		SmartDashboard::PutNumber("shooter roll", shooter.Roll());
		SmartDashboard::PutNumber("operator y axis", operatorStick.GetRawAxis(1));
		SmartDashboard::PutBoolean("shooter shoot button pressed", shooterShootButtonPressed);
		SmartDashboard::PutBoolean("shooting", shooting);
		SmartDashboard::PutBoolean("Compy Switch", compressor.GetPressureSwitchValue());
		SmartDashboard::PutNumber("gear open timer", gearOpenTimer.Get());
		SmartDashboard::PutNumber("shooterSpeed", shooterSpeed);

		/*
		 *
		 * END SMART DASHBOARD
		 *
		 */

		frc::Wait(0.01); // wait 5ms to avoid hogging CPU cycles TODO: change back to .005 if necessary
		gyro.UpdateDisplacement(gyro.GetRawAccelX(), gyro.GetRawAccelY(), gyro.GetActualUpdateRate(), gyro.IsMoving()); //update gyro displacement

	}
	robotDrive.SetSafetyEnabled(true);
	shooter.disable();
	climber.disable();
	compressor.Stop();
}

void Robot::Autonomous() {
	//This function so far will go through and try to target onto the peg and try to run onto it.
	robotDrive.SetSafetyEnabled(false);
	gyro.ResetDisplacement();
	gyro.ZeroYaw();
	gear.setBottom(true); //TODO: may need to change to false
	Accumulator accum((float)0.0, (float)0.0, (float)0.0, 2, leftProx, rightProx, aimer, *encoder, pid);
	int failsafe = 0;
	float desiredAngle = 0.0;//This is the angle to which the robot will try to aim
	float angleChangle = 0.0;
	float yOutput = 0.0;
	float currentAngle = 0.0;
	float leftUltrasonic = leftProx.GetRangeInches();
	float rightUltrasonic = rightProx.GetRangeInches();
	bool isDone, started;
	switch(1)//(int)SmartDashboard::GetNumber("Starting Position", 1))//This gets the starting position from the user
	{
	case 1://Position 1: straight from the middle peg
		//I think we should just be able to go straight from here, dude
		/*desiredAngle = 0.0;
		isDone = false;
		started = false;
		while (!isDone && failsafe < 1000 && !IsOperatorControl()) {
			DoubleDouble driveVals = accum.drive(started, true, false, false, 10, 0, 2, gyro.GetYaw());
			started = true;
			float driveX = driveVals.x;
			float driveY = driveVals.y;
			float driveZ = driveVals.angle;
			robotDrive.MecanumDrive_Cartesian(driveX, driveY, driveZ); //drive forward while staying straight
			/*if (filter.ultrasonicFilter(leftUltrasonic, rightUltrasonic) < 12) { //TODO: change when we get the kalman filters on
				isDone = true; //stop the loop
				failsafe = 400;
			}*/
			/*if (driveY == 0 && driveX == 0)
				isDone = true;
			frc::Wait(.01); //wait to avoid hogging cpu cycles
			failsafe++; //increment failsafe variable
		}
		robotDrive.MecanumDrive_Cartesian(0.0, 0.0, 0.0); //stop robot
		failsafe = 0; //reset failsafe for use later*/
		break;
	case 2://Position 2: on the left
		desiredAngle = 300; //TODO: 300?
		isDone = false;
		while (!isDone && failsafe < 500 && !IsOperatorControl()) { //TODO: change failsafe < 500 to be the calculated value to go the desired distance
			currentAngle = gyro.GetYaw() < 0 ? 360 + gyro.GetYaw() : gyro.GetYaw(); //current angle
			angleChangle = pid.PIDAngle(currentAngle, 0.0); //drive straight
			robotDrive.MecanumDrive_Cartesian(0.0, -.5, angleChangle); //drive straight for a bit
			frc::Wait(.01);
			failsafe++;
		}
		robotDrive.MecanumDrive_Cartesian(0.0, 0.0, 0.0);
		failsafe = 0;
		while (!isDone && failsafe < 200 && !IsOperatorControl()) {
			currentAngle = gyro.GetYaw() < 0 ? 360 + gyro.GetYaw() : gyro.GetYaw(); //current angle
			leftUltrasonic = leftProx.GetRangeInches();
			rightUltrasonic = rightProx.GetRangeInches();
			angleChangle = pid.PIDAngle(currentAngle, desiredAngle); //angle drive value
			if (fabs(angleChangle) < .01) { //if done driving angle
				yOutput = pid.PIDY(leftUltrasonic, rightUltrasonic); //y drive value
			} else {
				yOutput = 0.0; //don't drive y until done driving angle
			}
			if (fabs(angleChangle) < .01 && fabs(yOutput) < .01) { //exit loop
				failsafe = 200;
				isDone = true;
			}
			robotDrive.MecanumDrive_Cartesian(0.0, yOutput, angleChangle); //turn and drive
			frc::Wait(.01); //wait to avoid hogging cpu cycles
			failsafe++;
		}
		robotDrive.MecanumDrive_Cartesian(0.0, 0.0, 0.0); //stop moving
		failsafe = 0; //reset failsafe
		break;
	case 3://Position 2: on the left
		desiredAngle = 60; //TODO: 300?
		isDone = false;
		while (!isDone && failsafe < 500 && !IsOperatorControl()) { //TODO: change failsafe < 500 to be the calculated value to go the desired distance
			currentAngle = gyro.GetYaw() < 0 ? 360 + gyro.GetYaw() : gyro.GetYaw(); //current angle
			angleChangle = pid.PIDAngle(currentAngle, 0.0); //drive straight
			robotDrive.MecanumDrive_Cartesian(0.0, -.5, angleChangle); //drive straight for a bit
			frc::Wait(.01);
			failsafe++;
		}
		robotDrive.MecanumDrive_Cartesian(0.0, 0.0, 0.0);
		failsafe = 0;
		while (!isDone && failsafe < 200 && !IsOperatorControl()) {
			currentAngle = gyro.GetYaw() < 0 ? 360 + gyro.GetYaw() : gyro.GetYaw(); //current angle
			leftUltrasonic = leftProx.GetRangeInches();
			rightUltrasonic = rightProx.GetRangeInches();
			angleChangle = pid.PIDAngle(currentAngle, desiredAngle); //angle drive value
			if (fabs(angleChangle) < .01) { //if done driving angle
				yOutput = pid.PIDY(leftUltrasonic, rightUltrasonic); //y drive value
			} else {
				yOutput = 0.0; //don't drive y until done driving angle
			}
			if (fabs(angleChangle) < .01 && fabs(yOutput) < .01) { //exit loop
				failsafe = 200;
				isDone = true;
			}
			robotDrive.MecanumDrive_Cartesian(0.0, yOutput, angleChangle); //turn and drive
			frc::Wait(.01); //wait to avoid hogging cpu cycles
			failsafe++;
		}
		robotDrive.MecanumDrive_Cartesian(0.0, 0.0, 0.0); //stop moving
		failsafe = 0; //reset failsafe
		break;
	}

	float bigFailsafe = 0;
	failsafe = 0;
	DoubleDouble driveVals(0, 0, 0);
	while(failsafe < 500 && driveVals.x > 0.5)
	{
		driveVals = accum.drive(true, true, false, false, 12, 0, 2, gyro.GetYaw() < 0 ? 360 + gyro.GetYaw() : gyro.GetYaw());
		robotDrive.MecanumDrive_Cartesian(driveVals.x, driveVals.y, driveVals.angle);
		failsafe++;
	}
	/*while (!(fabs(aimer.TwoCameraAngleFilter()) <= 3.0) && bigFailsafe < 500 && !IsOperatorControl())//This adjusts the accuracy of the aiming of the robot TODO: change to whatever we're actually using for the camera
	{
		int sign = (aimer.TwoCameraAngleFilter() < 0) ? -1 : 1;//Which direction to turn
		while (fabs(currentAngle - desiredAngle) < 3 && failsafe < 100 && !IsOperatorControl())
		{
			currentAngle = gyro.GetYaw() < 0 ? 360 + gyro.GetYaw() : gyro.GetYaw(); //current angle
			desiredAngle = aimer.TwoCameraAngleFilter() + currentAngle;
			desiredAngle = desiredAngle < 0 ? desiredAngle + 360 : desiredAngle > 360 ? desiredAngle - 360 : desiredAngle; //nested question mark operator - bada bop bop ba, I'm lovin' it - TODO: change to whatever we're actually using for the camera
			float angleChangle = pid.PIDAngle(currentAngle, desiredAngle);
			robotDrive.MecanumDrive_Cartesian(0.0, 0.0, angleChangle);
			SmartDashboard::PutNumber("Angle to Gear", aimer.TwoCameraAngleFilter()); //TODO: change if we use something else for the cameras
			frc::Wait(.01);
			failsafe++;
			bigFailsafe++;
		}
		failsafe = 0;
		while (gyro.GetYaw() + aimer.TwoCameraAngleFilter() >= desiredAngle - 3 && gyro.GetYaw() + aimer.TwoCameraAngleFilter() <= desiredAngle + 3 && failsafe < 200 && !IsOperatorControl())
		{
			angleChangle = pid.PIDAngle(gyro.GetYaw(), aimer.TwoCameraAngleFilter() + gyro.GetYaw() + 20 * sign);
			robotDrive.MecanumDrive_Cartesian(0, -0.5, angleChangle);
			SmartDashboard::PutNumber("Angle to Gear", aimer.TwoCameraAngleFilter());
			frc::Wait(.01);
			failsafe++;
		}
		failsafe = 0;
		while (!(currentAngle >= desiredAngle) && failsafe < 100 && !IsOperatorControl())
		{
			currentAngle = gyro.GetYaw() < 0 ? 360 + gyro.GetYaw() : gyro.GetYaw(); //current angle
			angleChangle = pid.PIDAngle(gyro.GetYaw(), aimer.TwoCameraAngleFilter() + gyro.GetYaw());
			robotDrive.MecanumDrive_Cartesian(0, 0.0, angleChangle);
			SmartDashboard::PutNumber("Angle to Gear", aimer.TwoCameraAngleFilter());
			frc::Wait(.01);
			failsafe++;
		}
		failsafe = 0;
	}
	 */
	/*while(/*filter.ultrasonicFilter(leftUltrasonic, rightUltrasonic) > 12 && *//*failsafe < 200 && !IsOperatorControl())
	{
		float angleChangle = pid.PIDAngle(gyro.GetYaw(), aimer.TwoCameraAngleFilter() + gyro.GetYaw()); //TODO: change to joseph's accumulator code
		float driveSpeed = 0.5 - ((getAverageDistance(leftProx, rightProx) / 80) - 0.5);
		robotDrive.MecanumDrive_Cartesian(0, -driveSpeed, angleChangle);
		SmartDashboard::PutNumber("Angle to Gear", aimer.TwoCameraAngleFilter());
		SmartDashboard::PutNumber("leftProx", leftUltrasonic);
		SmartDashboard::PutNumber("rightProx", rightUltrasonic);
		SmartDashboard::PutNumber("leftIR", leftIR.get());
		SmartDashboard::PutNumber("rightIR", rightIR.get());
		//SmartDashboard::PutNumber("Ultrasonic Filter", filter.ultrasonicFilter(leftUltrasonic, rightUltrasonic));
		frc::Wait(.01);
		failsafe++;
	}*/

	//TODO: drop gear, or pretend to
	gear.setBottom(false); //TODO: may need to switch to true
	frc::Wait(.5);
	//Woa, woa, woa, back it up
	failsafe = 0;
	isDone = false;
	while (!isDone && failsafe < 100 && !IsOperatorControl()) { //TODO: change failsafe < 200 to be the calculated value to go the desired distance
		currentAngle = gyro.GetYaw() < 0 ? 360 + gyro.GetYaw() : gyro.GetYaw(); //current angle
		angleChangle = pid.PIDAngle(currentAngle, 0.0); //drive straight
		robotDrive.MecanumDrive_Cartesian(0.0, 0.5, angleChangle); //drive straight (backwards) for a bit
		frc::Wait(.01);
		failsafe++;
	}
	failsafe = 0;
	isDone = false;
	//find boilerino
	//Turn until you can see the target(FOREVER)
	while(aimer.GetBoilerAge() > 1 && failsafe <= 500 && !IsOperatorControl())
	{
		robotDrive.MecanumDrive_Cartesian(0, 0, 0.31415926);
		failsafe++;
		frc::Wait(.01);
	}
	robotDrive.MecanumDrive_Cartesian(0.0, 0.0, 0.0);
	//turn to boiling point
	failsafe = 0;
	pid.resetPIDAngle(); //if loop is done reset values
	float angle = gyro.GetYaw() < 0 ? 360 + gyro.GetYaw() : gyro.GetYaw();
	float angleOutput;
	float desireAngle = aimer.GetBoilerAngle() + angle;
	desireAngle = desireAngle < 0 ? desireAngle + 360 : desireAngle > 360 ? desireAngle - 360 : desireAngle; //nested question mark operator - bada bop bop ba, I'm lovin' it - TODO: change to whatever we're actually using for the camera
	while(failsafe < 500 && fabs(desireAngle - angle) > 1) //turn to boiler
	{
		angle = gyro.GetYaw() < 0 ? 360 + gyro.GetYaw() : gyro.GetYaw();
		angleOutput = pid.PIDAngle(angle, desireAngle);
		robotDrive.MecanumDrive_Cartesian(0,0,angleOutput);
		failsafe++;
		frc::Wait(.01);
	}
	failsafe = 0;
	shooter.shoot(.5); //TODO: change shooter speed
	//We should have it be stuck in a while loop until all of the balls have been shot.  We should tell the electrical team to put a sensor on the robot to tell us if we're able to shoot anymore.
	int theVariableThatTellsTheProgramTheDistanceToTheBoilerFromHopper = 55;

	while(aimer.GetBoilerDistance() > theVariableThatTellsTheProgramTheDistanceToTheBoilerFromHopper + 10 || aimer.GetBoilerDistance() < theVariableThatTellsTheProgramTheDistanceToTheBoilerFromHopper - 10 && failsafe < 500)
	{
		float sanicSpead;
		if(aimer.GetBoilerDistance() < theVariableThatTellsTheProgramTheDistanceToTheBoilerFromHopper)
		{
			sanicSpead = -.2;
		}else sanicSpead = .2;
		//might hafta go fest
		if(aimer.GetBoilerDistance() < theVariableThatTellsTheProgramTheDistanceToTheBoilerFromHopper - 20 || aimer.GetBoilerDistance() > theVariableThatTellsTheProgramTheDistanceToTheBoilerFromHopper + 20)
			sanicSpead *= 2;
		robotDrive.MecanumDrive_Cartesian(0,sanicSpead,0);
		failsafe++;
		frc::Wait(.01);
	}
	failsafe = 0;
	robotDrive.MecanumDrive_Cartesian(0,0,0);
	failsafe = 0;
	pid.resetPIDAngle(); //if loop is done reset values
	angle = gyro.GetYaw() < 0 ? 360 + gyro.GetYaw() : gyro.GetYaw();
	desireAngle = 180 + angle;
	desireAngle = desireAngle < 0 ? desireAngle + 360 : desireAngle > 360 ? desireAngle - 360 : desireAngle; //nested question mark operator - bada bop bop ba, I'm lovin' it - TODO: change to whatever we're actually using for the camera
	while(failsafe < 500 && fabs(desireAngle - angle) > 1) //Make sho you're zeroed to hero
	{
		angle = gyro.GetYaw() < 0 ? 360 + gyro.GetYaw() : gyro.GetYaw();
		angleOutput = pid.PIDAngle(angle, desireAngle);
		robotDrive.MecanumDrive_Cartesian(0,0,angleOutput);
		failsafe++;
		frc::Wait(.01);
	}
	failsafe = 0;
	robotDrive.MecanumDrive_Cartesian(0,0,0);
	//are there sideways ultra sanics?
	float distanceToWall = /*Get distance to hopper from left ultra sonic.  Convert to inches if needed*/ 0;
	encoder->Reset();
	while(encoder->GetDistance() < distanceToWall - 10)
	{
		robotDrive.MecanumDrive_Cartesian(0, 0.5, 0);
	}
	robotDrive.MecanumDrive_Cartesian(0,0,0);
	shooter.shoot(.5); //TODO: change shooter speed
	robotDrive.SetSafetyEnabled(true);
}

inline float getAverageDistance(const Ultrasonic& leftProx, const Ultrasonic& rightProx)
{
	return ((float)leftProx.GetRangeInches() + (float)rightProx.GetRangeInches()) / 2.0;
}

START_ROBOT_CLASS(Robot)
