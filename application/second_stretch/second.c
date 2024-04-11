#include "second.h"
#include "robot_def.h"
#include "dji_motor.h"
#include "ins_task.h"
#include "message_center.h"
#include "general_def.h"
#include "encoder.h"

#include "bmi088.h"

// static INS_Instance *gimbal_IMU_data; // 云台IMU数据
static DJIMotorInstance *left2_speed_motor, *right2_speed_motor;
static Publisher_t *second_stretch_pub;                          // 二级应用消息发布者(二级反馈给cmd)
static Subscriber_t *second_stretch_sub;                         // cmd控制消息订阅者
static Second_Stretch_Upload_Data_s second_stretch_feedback_data; // 回传给cmd的二级状态信息
static Second_Stretch_Ctrl_Cmd_s second_stretch_cmd_recv;         // 来自cmd的控制信息

void Second_Stretch_Init()
{
    // 一级左电机
    Motor_Init_Config_s second_stretch_left_config = {
        .can_init_config = {
            .can_handle = &hfdcan2,
            .tx_id      = 3,
        },
        .controller_param_init_config = {
            .angle_PID = {
                .Kp            = 0,
                .Ki            = 0,
                .Kd            = 0,
                .DeadBand      = 0.1f,
                .Improve       = PID_Trapezoid_Intergral | PID_Integral_Limit | PID_Derivative_On_Measurement,
                .IntegralLimit = 0,

                .MaxOut = 500,
            },
            .speed_PID = {
                .Kp            = 0,
                .Ki            = 0,
                .Kd            = 0,
                .Improve       = PID_Trapezoid_Intergral | PID_Integral_Limit | PID_Derivative_On_Measurement,
                .IntegralLimit = 0,
                .MaxOut        = 0,
            },
            //.other_angle_feedback_ptr = &left_angle_motor->measure.total_angle,
            // 还需要增加角速度额外反馈指针,注意方向,ins_task.md中有c板的bodyframe坐标系说明
            //.other_speed_feedback_ptr = &gimbal_IMU_data->INS_data.INS_gyro[INS_YAW_ADDRESS_OFFSET],
        },
        .controller_setting_init_config = {
            .angle_feedback_source = MOTOR_FEED,
            .speed_feedback_source = MOTOR_FEED,
            .outer_loop_type       = ANGLE_LOOP,
            .close_loop_type       = ANGLE_LOOP | SPEED_LOOP,
            .motor_reverse_flag    = MOTOR_DIRECTION_NORMAL,
        },
        .motor_type = M3508};
    // 一级右电机
    Motor_Init_Config_s second_stretch_right_config = {
        .can_init_config = {
            .can_handle = &hfdcan2,
            .tx_id      = 4,
        },
        .controller_param_init_config = {
            .angle_PID = {
                .Kp            = 0,
                .Ki            = 0,
                .Kd            = 0,
                .Improve       = PID_Trapezoid_Intergral | PID_Integral_Limit | PID_Derivative_On_Measurement,
                .IntegralLimit = 0,
                .MaxOut        = 0,
            },
            .speed_PID = {
                .Kp            = 0,
                .Ki            = 0,
                .Kd            = 0,
                .Improve       = PID_Trapezoid_Intergral | PID_Integral_Limit | PID_Derivative_On_Measurement,
                .IntegralLimit = 0,
                .MaxOut        = 0,
            },
            //.other_angle_feedback_ptr = &right_angle_motor->measure.total_angle,
            // 还需要增加角速度额外反馈指针,注意方向,ins_task.md中有c板的bodyframe坐标系说明
            //.other_speed_feedback_ptr = (&gimbal_IMU_data->INS_data.INS_gyro[INS_PITCH_ADDRESS_OFFSET]),
        },
        .controller_setting_init_config = {
            .angle_feedback_source = MOTOR_FEED,
            .speed_feedback_source = MOTOR_FEED,
            .outer_loop_type       = ANGLE_LOOP,
            .close_loop_type       = SPEED_LOOP | ANGLE_LOOP,
            .motor_reverse_flag    = MOTOR_DIRECTION_NORMAL,
        },
        .motor_type = M3508,
    };
    // 电机对total_angle闭环,上电时为零,会保持静止,收到遥控器数据再动
    right2_speed_motor = DJIMotorInit(&second_stretch_right_config);
    left2_speed_motor  = DJIMotorInit(&second_stretch_left_config);

    second_stretch_pub = PubRegister("second_stretch_feed", sizeof(Second_Stretch_Upload_Data_s));
    second_stretch_sub = SubRegister("second_stretch_cmd", sizeof(Second_Stretch_Ctrl_Cmd_s));
}

/* 机器人云台控制核心任务,后续考虑只保留IMU控制,不再需要电机的反馈 */
void Second_Stretch_Task()
{
    // 获取云台控制数据
    // 后续增加未收到数据的处理
    SubGetMessage(second_stretch_sub, &second_stretch_cmd_recv);

    // @todo:现在已不再需要电机反馈,实际上可以始终使用IMU的姿态数据来作为云台的反馈,yaw电机的offset只是用来跟随底盘
    // 根据控制模式进行电机反馈切换和过渡,视觉模式在robot_cmd模块就已经设置好,gimbal只看yaw_ref和pitch_ref
    switch (second_stretch_cmd_recv.second_stretch_mode) {
        // 停止
        case SECOND_STOP:
            DJIMotorStop(left2_speed_motor);
            DJIMotorStop(right2_speed_motor);
            break;
        // 使用陀螺仪的反馈,底盘根据yaw电机的offset跟随云台或视觉模式采用
        case SECOND_STRETCH: // 后续只保留此模式
            DJIMotorEnable(left2_speed_motor);
            DJIMotorEnable(right2_speed_motor);
            //DJIMotorChangeFeed(left2_speed_motor, ANGLE_LOOP, OTHER_FEED);
            // DJIMotorChangeFeed(left_speed_motor, SPEED_LOOP, OTHER_FEED);
           // DJIMotorChangeFeed(right2_speed_motor, ANGLE_LOOP, OTHER_FEED);
            // DJIMotorChangeFeed(right_speed_motor, SPEED_LOOP, OTHER_FEED);
            DJIMotorSetRef(left2_speed_motor, second_stretch_cmd_recv.second_left); // yaw和pitch会在robot_cmd中处理好多圈和单圈
            DJIMotorSetRef(right2_speed_motor, second_stretch_cmd_recv.second_right);
            break;
        default:
            break;
    }

    // 在合适的地方添加pitch重力补偿前馈力矩
    // 根据IMU姿态/pitch电机角度反馈计算出当前配重下的重力矩
    // ...

    // 设置反馈数据,主要是imu和yaw的ecd
    second_stretch_feedback_data.new_left_angle  = left2_speed_motor->measure.total_angle;
    second_stretch_feedback_data.new_right_angle = right2_speed_motor->measure.total_angle;

    // 推送消息
    PubPushMessage(second_stretch_pub, (void *)&second_stretch_feedback_data);
}