DIS: MOV R5,#7FH
     MOV R6,#0FEH
DIS1:ACALL DE
     MOV A,R5
     RL A
     MOV R5,A
     MOV A,R6
     RR A
     MOV R6,A
     ANL A,R5
     MOV P1,A
     MOV P3,A
     SJMP DIS1

;********************************
DE: MOV R4,#5 ;ÑÓÊ±×Ó³ÌÐò
DE0:MOV R3,#20
DE1:MOV R2,#250
    DJNZ R2,$
    DJNZ R3,DE1
    DJNZ R4,DE0
    RET
