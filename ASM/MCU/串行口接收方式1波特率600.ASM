;**********************
ORG 0000H
SJMP START
ORG 0023H
SJMP R
;**********************
START: MOV SCON,#50H
       MOV TMOD,#20H
       MOV TL1,#0CDH
       MOV TH1,#0CDH
       MOV R0,#30H
       SETB TR1
       SETB EA
       SETB ES
WAIT0: JNB P1.0,DISS
       SJMP WAIT0
;**********************
DISS:MOV DPTR,#TAB
DIS0: MOV R0,#30H
MOV P2,#01H
DIS: MOV A,@R0
MOVC A,@A+DPTR
MOV P0,A
INC R0
JB P2.7,DIS0
MOV A,P2
RL A
MOV P2,A
SJMP DIS 
;**********************
R: MOV A,SBUF
   CLR RI
   MOV @R0,A
   INC R0
   CJNE R0,#38H,NEXT
   CLR P1.0
   RETI
NEXT: JB RI,R
      SJMP NEXT
TAB: DB 0C0H 0F9H 0A4H 0B0H 99H 92H 82H 0F8H 80H 90H
;¶ÎÂë±í   0    1    2    3    4   5   6   7    8   9
END
   
