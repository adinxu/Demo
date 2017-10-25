void delay_us(register unsigned int dt)
{unsigned char i;
for(;dt;dt--)
for(i=1;i;i--);
}
//--------------------------------´úÂë·Ö¸îÏß----------------------------------
void delay_ms(unsigned int dt)
{
 unsigned int bt,ct;
for (; dt; dt--)
    for (ct=4;ct;ct--)
        for (bt=240; --bt; );
}