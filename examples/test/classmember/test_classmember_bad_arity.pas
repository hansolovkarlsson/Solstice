program TestCMBadArity;
type
    TMath = class
    public
        class function Square(n: integer): integer;
    end;

function TMath.Square;
begin
    Square := n * n;
end;

begin
    writeln(TMath.Square(1, 2));
end.
