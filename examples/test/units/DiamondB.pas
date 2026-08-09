unit DiamondB;

interface

uses Base;

function GetB: integer;

implementation

function GetB;
begin
  GetB := BaseVal + 2;
end;

end.
