unit Shapes;

interface

type
  TCircle = class
    radius: integer;
    function Area: integer;
  end;

implementation

function TCircle.Area;
begin
  Area := radius * radius * 3;
end;

end.
