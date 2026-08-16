program TestDefaultsForward;
{ a default lives on the forward declaration - the completing body omits
  the parameter list entirely, per this compiler's existing forward-
  completion convention, and still gets the default for free }
procedure Greet(name: string; times: integer = 2); forward;

procedure Greet;
var i: integer;
begin
    for i := 1 to times do
        writeln(name);
end;

begin
    Greet('Hi');     { twice, from the default }
    Greet('Yo', 1);  { once }
end.
