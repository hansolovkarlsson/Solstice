program TestProctypeReturnAsArg;

// The result of calling a procedural-returning function can be passed
// directly as an argument to another call expecting a procedural
// parameter, not just assigned to a variable first.
// Apply(GetDouble(), 7) = Double(7) = 14.

type
  TProc = function(x: integer): integer;

function Double(x: integer): integer;
begin
  Double := x * 2;
end;

function GetDouble: TProc;
begin
  GetDouble := Double;
end;

function Apply(function f(z: integer): integer; v: integer): integer;
begin
  Apply := f(v);
end;

begin
  writeln(Apply(GetDouble(), 7));
end.
