#include "stm32f10x.h"
#include "Knob.h"

#define Knob_Button_Port GPIOB
#define Knob_Button_Pin GPIO_Pin_5
#define Knob_Button_EXTI EXTI_Line5

#define Knob_Roller_Port GPIOB
#define Knob_Roller_Pin1 GPIO_Pin_6
#define Knob_Roller_Pin2 GPIO_Pin_7
#define Knob_Roller_Pin1_EXIT EXTI_Line6
#define Knob_Roller_Pin2_EXIT EXTI_Line7

#define _TIME_POLLING_TIME_TIMER_MS			10		//当需要定时器轮询检测时，每隔10ms检测一次
#define _TIME_BOTTON_DEBOUNCE_TIMER_MS			10		//按键消抖时间，10ms
#define _TIME_BUTTON_RESET				20		//按键重置时间，20ms。在重置期间，定时器不断轮询检测是否弹起，如果在_TIME_BUTTON_RESET时间内的所有按键都弹起，则认为按键已经弹起，重置结束，否则重新重置
#define _TIME_LONG_PRESS_WAITING			700		//长按等待时间，700ms，在长按等待时间内，定时器不断轮询检测是否弹起，如果在_TIME_LONG_PRESS_WAITING时间内有两个连续的弹起，则认为按键已经弹起，不触发长按，否则进入长按循环触发
#define _TIME_LONG_PRESS_POLLING_TIMER			100		//长按轮询时间，100ms，定时器每隔100ms检测一次是否弹起，如果弹起，则退出长按循环触发，否则继续触发
#define _TIME_MULTICLICKS_WAITING			120		//连击等待时间，120ms，在连击等待时间内，定时器不断轮询检测是否按下，如果在_TIME_MULTICLICKS_WAITING时间内有两个连续的按下，则认为按键已经按下，按键连击数+1，否则根据连击数触发相应的连击事件
#define _TIME_ROLLER_POLLING_TIMER_US			300		//旋转编码器轮询时间，300us，定时器每隔100us检测一次
#define _TIME_ROLLER_DEBOUNCE_TIMER_US			300		//旋转编码器消抖时间，300us	
#define _TIME_ROLLER_OVER_TIME_CHECK_T			200		//最多轮询200次等待旋钮恢复

static uint32_t roller_overtime = 0;
static uint32_t current_time = 0;	//记录相对于当前事件开始后的时间
static uint32_t multiclicks = 0;	//记录连击数

enum _Knob_Last_Check//判断上一次检测是否符合要求(用于判断连续的两次轮询)
{
	_Satisfied,
	_Unsatisfied
} _Botton_Last_Check, _Roller_Last_Check;
enum _Knob_Timer_Task
{
	_TimerTask_Botton_Reset_Polling,						//按键重置定时器轮询检测
	_TimerTask_Botton_DeBounce,							//消除抖动并检测按键是否按下
	_TimerTask_LongPress_Waiting_Polling,						//长按等待定时器轮询检测
	_TimerTask_LongPress_Ongoing_Polling,						//长按进行定时器循环触发
	_TimerTask_MultiClicks_Waiting_Polling,						//连击等待定时器轮询检测
	_TimerTask_MultiClicks_WaitForRelease_Polling,					//连击等待按键弹起定时器轮询检测

	_TimerTask_Roller_Reset_Polling,						//旋转编码器重置定时器轮询检测
	_TimerTask_Roller_DeBounce_Pin1,						//旋转编码器消除抖动并检测旋转编码器是否旋转pin1
	_TimerTask_Roller_DeBounce_Pin2							//旋转编码器消除抖动并检测旋转编码器是否旋转pin2
} static _Timer_Task;
void _Knob_Button_Init()	//初始化旋钮按键
{
	//初始化GPIO
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_InitStructure.GPIO_Pin = Knob_Button_Pin;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(Knob_Button_Port, &GPIO_InitStructure);

	//初始化外部中断
	GPIO_EXTILineConfig(GPIO_PortSourceGPIOB, GPIO_PinSource5);
	EXTI_InitTypeDef EXTI_InitStructure;
	EXTI_InitStructure.EXTI_Line = EXTI_Line5;
	EXTI_InitStructure.EXTI_LineCmd = ENABLE;
	EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
	EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling;
	EXTI_Init(&EXTI_InitStructure);

	//初始化NVIC
	NVIC_InitTypeDef NVIC_InitStructure;
	NVIC_InitStructure.NVIC_IRQChannel = EXTI9_5_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
	NVIC_Init(&NVIC_InitStructure);
}
void _Knob_Timer_Init()	//初始化定时器
{
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

	TIM_InternalClockConfig(TIM2);

	TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
	TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
	TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
	TIM_TimeBaseInitStructure.TIM_Period = 0xffff;
	TIM_TimeBaseInitStructure.TIM_Prescaler = 7200 - 1;
	TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0;
	TIM_TimeBaseInit(TIM2, &TIM_TimeBaseInitStructure);

	TIM_ClearFlag(TIM2, TIM_FLAG_Update);
	TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);

	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
	NVIC_InitTypeDef NVIC_InitStructure;
	NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
	NVIC_Init(&NVIC_InitStructure);

	TIM_Cmd(TIM2, DISABLE);
}
void _Knob_Roller_Init()//初始化旋钮的旋转引脚
{
	//初始化GPIO
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_InitStructure.GPIO_Pin = Knob_Roller_Pin1 | Knob_Roller_Pin2;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(Knob_Roller_Port, &GPIO_InitStructure);
	GPIO_EXTILineConfig(GPIO_PortSourceGPIOB, GPIO_PinSource6);
	GPIO_EXTILineConfig(GPIO_PortSourceGPIOB, GPIO_PinSource7);

	EXTI_InitTypeDef EXTI_InitStructure;
	EXTI_InitStructure.EXTI_Line = Knob_Roller_Pin1_EXIT | Knob_Roller_Pin2_EXIT;
	EXTI_InitStructure.EXTI_LineCmd = ENABLE;
	EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
	EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling;
	EXTI_Init(&EXTI_InitStructure);
}
void _Knob_Button_EXTI_Cmd(FunctionalState sta)	//开关按钮引脚的中断
{	
	EXTI_InitTypeDef EXTI_InitStructure;
	EXTI_InitStructure.EXTI_Line = EXTI_Line5;
	EXTI_InitStructure.EXTI_LineCmd = sta;
	EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
	EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling;
	EXTI_Init(&EXTI_InitStructure);
}
void _Knob_Timer_ShutDown(void)		//关闭定时器及其中断
{
	NVIC_DisableIRQ(TIM2_IRQn);
	TIM_Cmd(TIM2, DISABLE);
	TIM_SetCounter(TIM2, 0);
	TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
	TIM_ITConfig(TIM2, TIM_IT_Update, DISABLE);;
}
void _Knob_Roller_EXTI_Cmd(FunctionalState sta)	//旋钮旋转引脚的中断
{
	EXTI_InitTypeDef EXTI_InitStructure;
	EXTI_InitStructure.EXTI_Line = Knob_Roller_Pin1_EXIT | Knob_Roller_Pin2_EXIT;
	EXTI_InitStructure.EXTI_LineCmd = sta;
	EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
	EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling;
	EXTI_Init(&EXTI_InitStructure);
}
void _Knob_Timer_Restart_ms(uint32_t time, enum _Knob_Timer_Task task)			//在time毫秒后触发定时器中断
{
	_Timer_Task = task;
	//NVIC_DisableIRQ(TIM2_IRQn);
	TIM_Cmd(TIM2, DISABLE);
	TIM_SetCounter(TIM2, 0);
	TIM_SetAutoreload(TIM2, time * 10);
	//由于定时器的时钟频率为72MHz，分配器为7200，所以counter每0.1ms加1
	//time 的单位为ms，所以要乘以10
	TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
	TIM_ClearFlag(TIM2, TIM_FLAG_Update);
	TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);
	NVIC_EnableIRQ(TIM2_IRQn);
	TIM_Cmd(TIM2, ENABLE);
}
void _Knob_Timer_Restart_us(uint32_t time, enum _Knob_Timer_Task task)			//在time微秒后触发定时器中断
{
	_Timer_Task = task;
	NVIC_DisableIRQ(TIM2_IRQn);
	TIM_Cmd(TIM2, DISABLE);
	TIM_SetCounter(TIM2, 0);
	time = (time <= 100) ? 1 : time / 100;
	TIM_SetAutoreload(TIM2, time);
	//由于定时器的时钟频率为72MHz，分配器为7200，所以counter每0.1ms即100us加1
	//time 的单位为us，所以要除以100
	TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
	TIM_ClearFlag(TIM2, TIM_FLAG_Update);
	TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);
	NVIC_EnableIRQ(TIM2_IRQn);
	TIM_Cmd(TIM2, ENABLE);
}
uint8_t _IsButtonReleased(void)
{
	return (GPIO_ReadInputDataBit(Knob_Button_Port, Knob_Button_Pin) == 1) ? 1 : 0;
}
uint8_t _IsButtonPressed(void)
{
	return (GPIO_ReadInputDataBit(Knob_Button_Port, Knob_Button_Pin) == 0) ? 1 : 0;
}
uint8_t _IsRollerActive(void)	//如果两个旋钮引脚都为低电平(非空闲)，则返回1
{
	return (GPIO_ReadInputDataBit(Knob_Roller_Port, Knob_Roller_Pin1) == 0 && GPIO_ReadInputDataBit(Knob_Roller_Port, Knob_Roller_Pin2) == 0) ? 1 : 0;
}
uint8_t _IsRollerIdle(void)		//如果两个旋钮引脚都为高电平(空闲)，则返回1
{
	return (GPIO_ReadInputDataBit(Knob_Roller_Port, Knob_Roller_Pin1) == 1 && GPIO_ReadInputDataBit(Knob_Roller_Port, Knob_Roller_Pin2) == 1) ? 1 : 0;
}
void _Knob_Button_Status_Reset(void)//重置按钮状态
{
	current_time = 0;
	multiclicks = 0;
	_Botton_Last_Check = _Unsatisfied;
}
void _Knob_ClearITPendingBits(void)//清除所有中断标志位
{
	EXTI_ClearITPendingBit(Knob_Button_EXTI);
	EXTI_ClearITPendingBit(Knob_Roller_Pin1_EXIT);
	EXTI_ClearITPendingBit(Knob_Roller_Pin2_EXIT);
}
void Knob_Init(void)
{
	//开启外设时钟
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
	_Knob_Button_Init();
	_Knob_Timer_Init();
	_Knob_Roller_Init();
	_Knob_Button_EXTI_Cmd(DISABLE);
	_Botton_Last_Check = _Unsatisfied;
	_Knob_Timer_Restart_ms(_TIME_BOTTON_DEBOUNCE_TIMER_MS, _TimerTask_Botton_Reset_Polling);
}
void EXTI9_5_IRQHandler(void)
{
	if (EXTI_GetITStatus(Knob_Roller_Pin1_EXIT) == SET)
	{
		_Knob_ClearITPendingBits();
		_Knob_Button_Status_Reset();	//重置按钮状态
		_Knob_Button_EXTI_Cmd(DISABLE);	//关闭按钮中断，在检测旋转时不需要检测按钮
		_Knob_Roller_EXTI_Cmd(DISABLE);	//关闭旋转中断，在检测旋转时不需要检测旋转
		_Knob_Timer_Restart_us(_TIME_ROLLER_DEBOUNCE_TIMER_US, _TimerTask_Roller_DeBounce_Pin1);
	}
	else if (EXTI_GetITStatus(Knob_Roller_Pin2_EXIT) == SET)
	{
		_Knob_ClearITPendingBits();
		_Knob_Button_Status_Reset();	//重置按钮状态
		_Knob_Button_EXTI_Cmd(DISABLE);	//关闭按钮中断，在检测旋转时不需要检测按钮
		_Knob_Roller_EXTI_Cmd(DISABLE);	//关闭旋转中断，在检测旋转时不需要检测旋转
		_Knob_Timer_Restart_us(_TIME_ROLLER_DEBOUNCE_TIMER_US, _TimerTask_Roller_DeBounce_Pin2);
	}
	else if (EXTI_GetITStatus(Knob_Button_EXTI) == SET)
	{
		EXTI_ClearITPendingBit(Knob_Button_EXTI);
		_Knob_Button_EXTI_Cmd(DISABLE);
		_Knob_Timer_Restart_ms(_TIME_BOTTON_DEBOUNCE_TIMER_MS, _TimerTask_Botton_DeBounce);
	}
}
void TIM2_IRQHandler(void)
{
	if (TIM_GetITStatus(TIM2, TIM_IT_Update) != SET)
		return;
	TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
	switch (_Timer_Task)
	{
	case _TimerTask_Botton_Reset_Polling:
		if (_IsButtonReleased())//如果检测到按键空闲
		{
			current_time += _TIME_POLLING_TIME_TIMER_MS;
			if (current_time >= _TIME_BUTTON_RESET)//且在_TIME_BUTTON_RESET时间内按键都空闲
			{
				_Knob_Timer_ShutDown();
				//按键重置完成
				current_time = 0;
				//关闭定时器，打开按键中断，进入按键空闲等待
				EXTI_ClearITPendingBit(Knob_Button_EXTI);
				_Knob_Button_EXTI_Cmd(ENABLE);
			}
			else//尚未达到_TIME_BUTTON_RESET空闲时间
			{
				_Knob_Timer_Restart_ms(_TIME_POLLING_TIME_TIMER_MS, _TimerTask_Botton_Reset_Polling);
			}
		}
		else//检测到按键占用
		{
			current_time = 0;
			_Knob_Timer_Restart_ms(_TIME_POLLING_TIME_TIMER_MS, _TimerTask_Botton_Reset_Polling);
		}
		break;
	case _TimerTask_Botton_DeBounce:
		if (_IsButtonPressed())	//如果按键按下，则触发单击事件，并进入长按等待
		{
			Knob_Event_Click();
			_Knob_Timer_Restart_ms(_TIME_POLLING_TIME_TIMER_MS, _TimerTask_LongPress_Waiting_Polling);
		}
		else//如果按键未按下，则说明出错，进行按键重置
		{
			_Knob_Timer_Restart_ms(_TIME_POLLING_TIME_TIMER_MS, _TimerTask_Botton_Reset_Polling);
		}
		break;
	case _TimerTask_LongPress_Waiting_Polling:
		if (current_time <= _TIME_LONG_PRESS_WAITING)//处于长按等待时间内
		{
			current_time += _TIME_POLLING_TIME_TIMER_MS;
			uint8_t IsButtonReleased = _IsButtonReleased();
			if (IsButtonReleased && _Botton_Last_Check == _Satisfied)//检测到连续的两次按键抬起
			{
				current_time = 0;
				_Botton_Last_Check = _Unsatisfied;
				//进入连击等待
				_Knob_Timer_Restart_ms(_TIME_POLLING_TIME_TIMER_MS, _TimerTask_MultiClicks_Waiting_Polling);
			}
			else
			{
				_Botton_Last_Check = IsButtonReleased ? _Satisfied : _Unsatisfied;
			}
		}
		else//长按等待时间到
		{
			//触发长按事件
			Knob_Event_Long();
			//进入长按持续
			current_time = 0;
			_Botton_Last_Check = _Unsatisfied;
			_Knob_Timer_Restart_ms(_TIME_LONG_PRESS_POLLING_TIMER, _TimerTask_LongPress_Ongoing_Polling);
		}
		break;
	case _TimerTask_LongPress_Ongoing_Polling:
		if (_IsButtonPressed())
		{
			Knob_Event_Long();
			_Knob_Timer_Restart_ms(_TIME_LONG_PRESS_POLLING_TIMER, _TimerTask_LongPress_Ongoing_Polling);
		}
		else
		{
			_Knob_Timer_Restart_ms(_TIME_POLLING_TIME_TIMER_MS, _TimerTask_Botton_Reset_Polling);
		}
		break;
	case _TimerTask_MultiClicks_Waiting_Polling:
		
		if (current_time <= _TIME_MULTICLICKS_WAITING)
		{
			current_time += _TIME_POLLING_TIME_TIMER_MS;
			uint8_t IsButtonPressed = _IsButtonPressed();
			if (IsButtonPressed && _Botton_Last_Check == _Satisfied)//检测到连续的两次按键抬起
			{
				current_time = 0;
				_Botton_Last_Check = _Unsatisfied;
				multiclicks++;
				//进入连击松开等待
				_Knob_Timer_Restart_ms(_TIME_POLLING_TIME_TIMER_MS, _TimerTask_MultiClicks_WaitForRelease_Polling);
			}
			else
			{
				_Botton_Last_Check = IsButtonPressed ? _Satisfied : _Unsatisfied;
			}
		}
		else
		{
			//触发连击事件
			Knob_Event_MultiClicks(multiclicks);
			multiclicks = 0;
			//进入按键重置
			current_time = 0;
			_Botton_Last_Check = _Unsatisfied;
			_Knob_Timer_Restart_ms(_TIME_POLLING_TIME_TIMER_MS, _TimerTask_Botton_Reset_Polling);
		}
		break;
	case _TimerTask_MultiClicks_WaitForRelease_Polling:
		if (_IsButtonReleased())
		{
			_Knob_Timer_Restart_ms(_TIME_POLLING_TIME_TIMER_MS, _TimerTask_MultiClicks_Waiting_Polling);
		}
		else
		{
			_Knob_Timer_Restart_ms(_TIME_POLLING_TIME_TIMER_MS, _TimerTask_MultiClicks_WaitForRelease_Polling);
		}
		break;
		
	case _TimerTask_Roller_Reset_Polling:
		if(roller_overtime <= _TIME_ROLLER_OVER_TIME_CHECK_T)
		{
			if (_IsRollerIdle())
			{
				if (_Roller_Last_Check == _Satisfied)//检测到了两次连续的空闲
				{
					roller_overtime = 0;
					_Knob_Timer_ShutDown();
					_Roller_Last_Check = _Unsatisfied;
					_Knob_ClearITPendingBits();
					_Knob_Button_EXTI_Cmd(ENABLE);
					_Knob_Roller_EXTI_Cmd(ENABLE);
				}
				else
				{
					_Roller_Last_Check = _Satisfied;
					roller_overtime ++;
					_Knob_Timer_Restart_us(_TIME_ROLLER_POLLING_TIMER_US, _TimerTask_Roller_Reset_Polling);
				}
			}
			else
			{
				_Roller_Last_Check = _Unsatisfied;
				roller_overtime ++;
				_Knob_Timer_Restart_us(_TIME_ROLLER_POLLING_TIMER_US, _TimerTask_Roller_Reset_Polling);
			}
		}
		else
		{
			roller_overtime = 0;
			_Knob_Timer_ShutDown();
			_Roller_Last_Check = _Unsatisfied;
			_Knob_ClearITPendingBits();
			_Knob_Button_EXTI_Cmd(ENABLE);
			_Knob_Roller_EXTI_Cmd(ENABLE);
		}
		break;
	case _TimerTask_Roller_DeBounce_Pin1:
		if (_IsRollerActive())//如果引脚都是低电平
		{
			if (_IsButtonPressed())
				Knob_Event_Rotate_CW_Pressed();
			else
				Knob_Event_Rotate_CW();
			_Knob_Timer_Restart_us(_TIME_ROLLER_POLLING_TIMER_US, _TimerTask_Roller_Reset_Polling);
		}
		else
		{	//回到等待中断的状态
			_Knob_ClearITPendingBits();
			_Knob_Button_EXTI_Cmd(ENABLE);
			_Knob_Roller_EXTI_Cmd(ENABLE);
		}
		break;
	case _TimerTask_Roller_DeBounce_Pin2:
		if (_IsRollerActive())//如果引脚都是低电平
		{
			if (_IsButtonPressed())
				Knob_Event_Rotate_CCW_Pressed();
			else
				Knob_Event_Rotate_CCW();
			_Knob_Timer_Restart_us(_TIME_ROLLER_POLLING_TIMER_US, _TimerTask_Roller_Reset_Polling);
		}
		else
		{	//回到等待中断的状态
			_Knob_ClearITPendingBits();
			_Knob_Button_EXTI_Cmd(ENABLE);
			_Knob_Roller_EXTI_Cmd(ENABLE);
		}
		break;
	default:
		break;
	}
}
