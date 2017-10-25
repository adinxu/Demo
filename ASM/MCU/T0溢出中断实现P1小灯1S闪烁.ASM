;********************************
ORG 0000H
SJMP START
ORG 000BH
SJMP DIS
;********************************
START: MOV R2,#00H
       MOV TMOD,#01H
       MOV TH0,#3CH
       MOV TL0,#0B0H
       SETB EA
       SETB ET0
       SETB TR0
WAIT:  SJMP $
DIS: INC R2
     MOV TH0,#3CH
     MOV TL0,#0B0H
     CJNE R2,#20,OUT
     MOV R2,#00H
     CPL P1.0
OUT: RETI
