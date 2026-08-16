program TestProctypeReturnBasic;

// A function can return a named procedural type. Assigning to the
// function's own name inside its body follows the same bare-reference
// rule as any other procedural-type assignment target (GetHandler :=
// Double;), and calling the function to use its returned value needs
// explicit '()' even though GetHandler itself takes no arguments - the
// disambiguator between "call this" and "take a bare reference to
// this". Double(5) = 10.

type
  TProc = function(x: integer): integer;

var h: TProc;

function Double(x: integer): integer;
begin
  Double := x * 2;
end;

function GetHandler: TProc;
begin
  GetHandler := Double;
end;

begin
  h := GetHandler();
  writeln(h(5));
end.
