program TestExitValue;
{ 'exit(value);' sets the function's return value and returns immediately,
  same as 'FuncName := value; exit;'. Expected output: 10, -1. }
function Foo(x: integer): integer;
begin
    if x < 0 then
        exit(-1);
    Foo := x * 2;
end;
begin
    writeln(Foo(5));
    writeln(Foo(-3));
end.
