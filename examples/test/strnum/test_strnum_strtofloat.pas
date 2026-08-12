program TestStrnumStrtofloat;
var
    x: real;
begin
    writeln(StrToFloat('3.14'):0:2);
    writeln(StrToFloat('  2.5  '):0:1);
    writeln(StrToFloat('1e3'):0:0);
    x := 7.25;
    writeln(StrToFloat(FloatToStr(x)):0:2);
end.
