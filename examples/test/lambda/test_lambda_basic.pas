program TestLambdaBasic;

{ Assign a lambda literal to a named procedural-type variable, call it.
  3 < 5 = true. }

type
    TCmp = function(a, b: integer): boolean;

var
    cmp: TCmp;

begin
    cmp := function(a, b: integer): boolean begin exit(a < b); end;
    writeln(cmp(3, 5));
end.
