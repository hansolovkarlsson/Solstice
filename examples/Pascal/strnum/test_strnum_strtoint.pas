program TestStrnumStrtoint;
var
    n: integer;
begin
    writeln(StrToInt('42'));
    writeln(StrToInt('-17'));
    writeln(StrToInt('  -17 '));
    n := 12345;
    writeln(StrToInt(IntToStr(n)) = n);
end.
