program TestClassmemberMethodCall;
type
    TMath = class
    public
        class function Square(n: integer): integer;
        class procedure ShowSquare(n: integer);
    end;

function TMath.Square;
begin
    Square := n * n;
end;

procedure TMath.ShowSquare;
begin
    writeln('Square(', n, ') = ', TMath.Square(n));
end;

begin
    writeln(TMath.Square(7));
    TMath.ShowSquare(9);
end.
