# include "Com_Delay.h"


void Com_Delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}

void Com_Delay_s(uint32_t s)
{
    while (s--)
    {
        Com_Delay_ms(1000);
    }
    
}
