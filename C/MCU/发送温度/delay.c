void delay_us(register unsigned char dt)
{
while (--dt);//DJNZ每次用时2us
}
void delay_ms(unsigned int dt)
{
register unsigned char bt,ct;
for (; dt; dt--)
    for (ct=2;ct;ct--)
        for (bt=250; --bt; );
}