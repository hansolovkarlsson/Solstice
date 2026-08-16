10 REM Comprehensive milestone-1 smoke test
20 LET A = 5
30 LET B% = 3
40 PRINT "A+B*2="; A + B% * 2
50 IF A > B% THEN PRINT "A bigger" ELSE PRINT "B bigger or equal"
60 FOR I% = 1 TO 5
70 PRINT I%;
80 NEXT I%
90 PRINT
100 GOSUB 200
110 PRINT "back from sub"
120 LET S$ = "foo" + "bar"
130 PRINT S$
140 GOTO 160
150 PRINT "skipped"
160 END
200 PRINT "in sub"
210 RETURN
