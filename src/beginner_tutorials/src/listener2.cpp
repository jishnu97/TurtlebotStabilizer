#include "ros/ros.h"
#include "float.h"
#include <cmath>
#include "std_msgs/String.h"
#include "geometry_msgs/Twist.h"
#include "nav_msgs/Odometry.h"
#include "sensor_msgs/Imu.h"
#include "rl_msgs/RLAction.h"
#include "rl_msgs/RLEnvDescription.h"
#include "rl_msgs/RLStateReward.h"

struct StateReward{
//Teleop values
float teleopLinearXIn;
float teleopAngularZIn;
float teleopLinearXOut;
float teleopAngularZOut;

//IMU1 values
float imu1LinearX;
float imu1LinearZ;

//IMU2 values
float imu2LinearX;
float imu2LinearZ;

//Odom values
float odomLinearX;
float odomAngularZ;

};
std::clock_t start;
int actionFrequency[6];
float prev_linear_x;
float prev_linear_y;
float prev_linear_z;
float prev_angular_x;
float prev_angular_y;
float prev_angular_z;
float totalReward = 0.0;
float numReward = 0.0;
float alpha = .5;
float oldLinAcc = 0.0;
float oldAngAcc = 0.0;
int act1 = 0;
int act2 = 0;
int act3 = 0;
int act4 = 0;
int act5 = 0;
int act6 = 0;

StateReward stateReward;
ros::Publisher pub;
ros::Publisher pubRLEnv;
ros::Publisher pubRLState;

//The following two methods calculate delta Time using the number of clock cycles per second
void startTimer()
{
	start = std::clock();
}
double stopTimer()
{
	double duration = ( std::clock() - start ) / (double) CLOCKS_PER_SEC;
	start = std::clock();
	return duration;
}
//Calculates the derivative of acceleration i.e. jerk
float calculateDerivative(float acc1, float acc2, double deltaTime)
{
	if(deltaTime == 0)
	{
		return 0.0;
	}	
	return (acc2-acc1)/(float)deltaTime;
}


float cancelNoise(float val, float low, float high)
{
        if(val>= low && val<=high)
        {
                return 0;
        }
        return val;
}
float rewardCalculator(StateReward st)
{
        numReward++;
        float maxReward = 10;
        float beta = 0.5;
        float lossImuSquared = pow(st.imu2LinearX - st.imu1LinearX, 2) +
                               pow(st.imu2LinearZ - st.imu1LinearZ, 2);
        float lossImu = pow(lossImuSquared, 0.5);
        float lossTeleopSquared = pow(10*st.teleopLinearXIn - 10*st.teleopLinearXOut, 2) +
                               pow(10*st.teleopAngularZIn - 10*st.teleopAngularZOut, 2);
        float lossTeleop  = pow(lossTeleopSquared, 0.5);
        float loss = beta*lossImu+(1-beta)*lossTeleop;
        float reward = maxReward -loss;
	printf("IMU:%f Teleop:%f\n", lossImu, lossTeleop);
	if (reward<0.0)
        {
                totalReward +=0.0;
                //printf( "Running average is: %6.4lf\n", totalReward/numReward);
                return 0.0;
        }
        totalReward +=reward;
        //printf( "Running average is: %6.4lf\n", totalReward/numReward);
        return reward;
}


void cmd_vel_Callback_teleop(const geometry_msgs::Twist& cmd_vel)
{
  //Set struct values
  stateReward.teleopLinearXIn = cmd_vel.linear.x;
  stateReward.teleopAngularZIn = cmd_vel.angular.z; 
  float dTime = stopTimer(); 
  float jerkLin = calculateDerivative(oldLinAcc, stateReward.odomLinearX, dTime);
  float jerkAng = calculateDerivative(oldAngAcc, stateReward.odomAngularZ,dTime);
  oldLinAcc = stateReward.odomLinearX;
  oldAngAcc = stateReward.odomAngularZ;
  startTimer();

  printf("Lin:%f ang:%f\n", jerkLin, jerkAng);
  rl_msgs::RLStateReward stateVar;
  //Set values to the state vector
  stateVar.state.push_back(10*stateReward.odomLinearX);
  stateVar.state.push_back(10*stateReward.odomAngularZ);
  stateVar.state.push_back(10*stateReward.teleopLinearXIn);
  stateVar.state.push_back(10*stateReward.teleopAngularZIn);
  stateVar.state.push_back(10*jerkLin);
  stateVar.state.push_back(10*jerkAng);
  stateVar.reward = rewardCalculator(stateReward);
  //publish the message
  pubRLState.publish(stateVar);

}


void cmd_vel_Callback_odom(const nav_msgs::Odometry& odom)
{
  //Set linear and angular accelerations in the struct
  stateReward.odomLinearX = odom.twist.twist.linear.x;
  stateReward.odomAngularZ = odom.twist.twist.angular.z+0.002;
}

void imu1_Callback(const sensor_msgs::Imu& imu1)
{
  //This was done to ensure that the IMU report 0.0 when stationary 
  stateReward.imu1LinearX = cancelNoise(imu1.linear_acceleration.x,0.0,-0.01);
  stateReward.imu1LinearZ = cancelNoise(imu1.linear_acceleration.z,0.0,-0.09);
   
}

void imu2_Callback(const sensor_msgs::Imu& imu2)
{
  //This was done to ensure that the IMU report 0.0 when stationary
  stateReward.imu2LinearX = cancelNoise(imu2.linear_acceleration.x,0.0,-0.01);
  stateReward.imu2LinearZ = cancelNoise(imu2.linear_acceleration.z,0.0,-0.09);
}
void actPrinter()
{
	//Prints the number and type of actions taken by the agent 
	printf("New\n");
	printf("Action 1(low lin):%d\n", act1);
	printf("Action 2(med lin):%d\n", act2);
	printf("Action 3(high lin):%d\n", act3);
	printf("Action 4(no speed):%d\n", act4);
	printf("Action 5(low ang):%d\n", act5);
	printf("Action 6(high ang):%d\n", act6);
}
void RLAction_Callback(const rl_msgs::RLAction& act)
{
	float low = 0.2;
	float med = 0.5;
	float high = 1;
	float maxOdom = 0.18;
	float minOdom = -0.18;
	geometry_msgs::Twist new_cmd_vel;
	//Sends the appropriate action to the turtlebot depending on the
	// command sent by the agent
	if (act.action==0) 
	{
		act1++;
		new_cmd_vel.linear.x = low*maxOdom;
  		new_cmd_vel.angular.x = 0;
	}
	else if(act.action==1)
	{
		act2++;
		new_cmd_vel.linear.x = med*maxOdom;
                new_cmd_vel.angular.x = 0;	
	}
	else if(act.action==2)
        {
		act3++;
		new_cmd_vel.linear.x = high*maxOdom;
                new_cmd_vel.angular.x = 0;
                
        }
	else if(act.action==3)
        {
		act4++;
                new_cmd_vel.linear.x = 0;
                new_cmd_vel.angular.x = 0;
        }
	else if(act.action==4)
        {
		act5++;
                new_cmd_vel.linear.x = 0;
                new_cmd_vel.angular.z = float(0.5); 
        }
	else if(act.action==5)
	{
		act6++;
		new_cmd_vel.linear.x = 0;
                new_cmd_vel.angular.z = float(-0.5);
	}
	new_cmd_vel.linear.y = 0;
	new_cmd_vel.linear.z = 0;
	new_cmd_vel.angular.x = 0;
	new_cmd_vel.angular.y = 0;
	//Set struct values
	stateReward.teleopLinearXOut = new_cmd_vel.linear.x;	
	stateReward.teleopAngularZOut = new_cmd_vel.angular.z;
	pub.publish(new_cmd_vel);
	//Print out the actions
	actPrinter();
}

void initStruct(StateReward* st)
{
//Initialize the struct
st->teleopLinearXIn = 0;
st->teleopAngularZIn = 0;
st->teleopLinearXOut = 0;
st->teleopAngularZOut= 0;

st->imu1LinearX = 0;
st->imu1LinearZ = 0;

st->imu2LinearX = 0;
st->imu2LinearZ = 0;

st->odomLinearX = 0;
st->odomAngularZ = 0;
}




int main(int argc, char **argv)
{
  startTimer();
  ros::init(argc, argv, "listener2");
  initStruct(&stateReward);
  ros::NodeHandle n;
  ros::Subscriber sub_odom = n.subscribe("odom", 1000, cmd_vel_Callback_odom);
  ros::Subscriber sub_imu1 = n.subscribe("/sim1/imu/data_raw", 1000, imu1_Callback);
  ros::Subscriber sub_imu2 = n.subscribe("/sim2/imu/data_raw", 1000, imu2_Callback);
  
  ros::Subscriber sub_RLAction = n.subscribe("/rl_agent/rl_action",1000, RLAction_Callback);
  
  pub = n.advertise<geometry_msgs::Twist>("cmd_vel_mux/input/teleop",1); 


  //Publihing to reinforcement learning agent
  pubRLState = n.advertise<rl_msgs::RLStateReward>("/rl_env/rl_state_reward", 1);
  //Advertises to rl_msgs/RLEnvDescription Message
  pubRLEnv = n.advertise<rl_msgs::RLEnvDescription>("/rl_env/rl_env_description", 1, true);

	rl_msgs::RLEnvDescription envDesc;
	//Sets up the environment for the agent
	envDesc.num_actions = 6;
	envDesc.num_states = 2000;
	envDesc.max_reward = 10;
	envDesc.reward_range = 10;
	envDesc.stochastic = false;
	envDesc.episodic = false;
	pubRLEnv.publish(envDesc);
  	ros::spin();

  

  return 0;
}
