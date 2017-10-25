DF1: MOV P3,#0FFH   
     MOV P1,#0FEH        
DIS: ACALL DE        
     JNB P1.7,DF2          
     MOV A,P1               
     RL A
     MOV P1,A
     SJMP DIS                   
DF2: MOV P1,#0FFH            
     MOV P3,#0FEH                 
DIS2:ACALL DE                
     JNB P3.7,DIS3                  
     MOV A,P3                       
     RL A
     MOV P3,A
     SJMP DIS2                          
DIS3:JNB P3.0,DF3                  
     RR A                                
     MOV P3,A                            
     ACALL DE                             
     SJMP DIS3                             
DF3: MOV P3,#0FFH                       
     MOV P1,#07FH                           
     MOV A,P1                                 
DIS4:ACALL DE                              
     RR A                                        
     MOV P1,A
     JNB p1.0,DIS                                  
     SJMP DIS4                                     
;***********************************
DE:  MOV R4,#100 ;ÑÓÊ±×Ó³ÌÐò
DE0: MOV R3,#20
DE1: MOV R2,#250
     DJNZ R2,$
     DJNZ R3,DE1
     DJNZ R4,DE0
     RET
     END

