program TestExitClassMethod;
{ 'exit(value);' works inside a class method exactly like an ordinary
  function - methods are just proc_table[] entries too. Expected output:
  105, -1. }
type
    TFoo = class
        function Check(x: integer): integer;
    end;
var f: TFoo;

function TFoo.Check;
begin
    if x < 0 then
        exit(-1);
    Check := x + 100;
end;

begin
    new(f);
    writeln(f.Check(5));
    writeln(f.Check(-5));
    dispose(f);
end.
