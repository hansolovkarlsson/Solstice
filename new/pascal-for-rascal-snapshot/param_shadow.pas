program ParamShadow;
var
    x: integer;

procedure test(x: integer);
begin
    writeln('inside test, x (local/param) = ', x);
    x := x + 100;
    writeln('after modifying local x = ', x);
end;

begin
    x := 5;
    writeln('global x before call = ', x);
    test(42);
    writeln('global x after call = ', x);
end.
