program TestClassCtorDispatch;
type
    TShape = class
        area: integer;
        function ComputeArea: integer;
    end;
    TSquare = class(TShape)
        side: integer;
        function ComputeArea: integer;
        procedure Init(s: integer);
    end;
var
    sq: TSquare;

function TShape.ComputeArea;
begin
    ComputeArea := 0;
end;

function TSquare.ComputeArea;
begin
    ComputeArea := side * side;
end;

procedure TSquare.Init;
begin
    side := s;
    { calls a method on self FROM WITHIN the constructor, after setting
      a field the method depends on - the class tag must already be
      written by the time this runs (dynamic dispatch needs it), and
      must resolve to TSquare's OWN ComputeArea override, not
      TShape's - proving both "tag set before constructor body runs"
      and "dispatch works mid-constructor" in one deterministic check. }
    area := ComputeArea;
end;

begin
    new(sq, Init(5));
    writeln('side: ', sq.side);
    writeln('area: ', sq.area);
    dispose(sq);
end.
