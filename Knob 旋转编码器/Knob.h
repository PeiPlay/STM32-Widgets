#ifndef __KNOB_H__
#define __KNOB_H__
#include "stm32f10x.h"
/*
    旋转编码器小组件
    可以识别：单击，连击，长按，松开时转动1，松开时转动2，按下时转动1，按下时转动2

    空闲时按下按键后触发“单击”，然后进入长按等待状态
        如果在长按等待事件内按键都没有弹起，那么将循环触发“长按”直到按键松开
        如果在长按等待事件内松开按键，则进入连击等待，等待连续击键
            如果在连击等待时间内按下按键，连击值+1，此时不会再触发长按等待，而是等待按键弹起后再次进入连击等待
            如果连击等待时间内没有按下按键，则视为按键已经空闲，此时触发“连击”，连击值将会以参数形式传入
    
    任意时刻触发“旋转”都会打断当前的按钮事件，并且根据按钮是否按下和旋转方向调用对应函数
*/
void Knob_Init(void);

void Knob_Event_Click(void);					//单击事件
void Knob_Event_MultiClicks(uint32_t clicks);	//连击事件
void Knob_Event_Long(void);						//长按事件
void Knob_Event_Rotate_CW(void);				//松开时顺时针旋转
void Knob_Event_Rotate_CCW(void);				//松开时逆时针旋转
void Knob_Event_Rotate_CW_Pressed(void);		//按下时顺时针旋转
void Knob_Event_Rotate_CCW_Pressed(void);		//按下时逆时针旋转

#endif // !__KNOB_H__
