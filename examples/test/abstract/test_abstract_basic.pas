program TestAbstractBasic;
type
    TShape = class
    public
        function Area: real; abstract;
        function Describe: string;
    end;
    TCircle = class(TShape)
    private
        FRadius: real;
    public
        function Area: real;
        procedure SetRadius(r: real);
    end;
    TSquare = class(TShape)
    private
        FSide: real;
    public
        function Area: real;
        procedure SetSide(s: real);
    end;
var
    c: TCircle;
    sq: TSquare;
    shape: TShape;

function TShape.Describe;
begin
    Describe := 'shape';
end;

function TCircle.Area;
begin
    Area := 3.14159 * FRadius * FRadius;
end;

procedure TCircle.SetRadius;
begin
    FRadius := r;
end;

function TSquare.Area;
begin
    Area := FSide * FSide;
end;

procedure TSquare.SetSide;
begin
    FSide := s;
end;

begin
    new(c);
    c.SetRadius(2.0);
    new(sq);
    sq.SetSide(3.0);

    { the flagship scenario: calling .Area through a TShape-typed
      variable, with TShape.Area itself abstract - dispatch resolves to
      whichever concrete descendant's runtime tag the variable actually
      holds, never TShape's own (nonexistent) implementation }
    shape := c;
    writeln(shape.Describe, ' area = ', shape.Area:0:2);
    shape := sq;
    writeln(shape.Describe, ' area = ', shape.Area:0:2);
end.
