program TestProctypeReturnBadSignature;

// Calling (with explicit '()') a function whose return type doesn't
// match the target procedural type - a clean compile error, not a
// crash. GetOther returns TOtherProc (function(x: integer): boolean),
// not TProc (function(x: integer): integer).

type
  TProc = function(x: integer): integer;
  TOtherProc = function(x: integer): boolean;

var h: TProc;

function IsPositive(x: integer): boolean;
begin
  IsPositive := x > 0;
end;

function GetOther: TOtherProc;
begin
  GetOther := IsPositive;
end;

begin
  h := GetOther();
end.
