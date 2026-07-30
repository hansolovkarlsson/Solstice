program DceArrayDivZero;
var dead: array[1..5] of integer;
begin
    dead[1] := 10 div 0;
    writeln('unreachable check');
end.
