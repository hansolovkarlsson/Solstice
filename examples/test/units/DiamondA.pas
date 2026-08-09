unit DiamondA;

interface

uses Base;

function GetA: integer;

implementation

function GetA;
begin
  GetA := BaseVal + 1;
end;

end.
