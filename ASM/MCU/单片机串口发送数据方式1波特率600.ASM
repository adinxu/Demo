MOV SCON,#40H
MOV TMOD,#20H
MOV TL1,#0CDH
MOV TH1,#0CDH
MOV DPTR,#TAB
T0: MOV R2,#0
T: MOV A,R2
   MOVC A,@A+DPTR
   SETB TR1
   MOV SBUF,A
WAIT: JBC TI,NEXT
      SJMP WAIT
NEXT: INC R2
      CJNE R2,#9,T
      ACALL DE
      SJMP T0
DE: MOV R3,#10
DE1: MOV R4,#20
DE2: MOV R5,#250
     DJNZ R5,$
     DJNZ R4,DE2
     DJNZ R3,DE1
     RET
TAB: DB "���"," ","51",0DH,0AH

