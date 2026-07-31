program TestCaseRecursion;
function Fact(n: integer): integer;
begin
    case n of
        0, 1: Fact := 1;
    else
        Fact := n * Fact(n - 1);
    end;
end;

begin
    writeln(Fact(5));  { 120 }
    writeln(Fact(0));  { 1 }
end.
