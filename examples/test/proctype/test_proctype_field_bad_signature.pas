program TestProctypeFieldBadSignature;

// Assigning a procedure/function whose signature doesn't match a
// class field's declared procedural type - a clean compile error, not
// a crash. IsPositive returns boolean, not integer, so it doesn't
// match TProc.

type
  TProc = function(x: integer): integer;
  TFoo = class
    handler: TProc;
  end;

var f: TFoo;

function IsPositive(x: integer): boolean;
begin
  IsPositive := x > 0;
end;

begin
  new(f);
  f.handler := IsPositive;
  dispose(f);
end.
