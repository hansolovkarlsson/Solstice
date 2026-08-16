unit IFDiamondA;

interface

uses IFBase;

function GetA: integer;

implementation

function GetA;
begin
  GetA := BaseVal + 1;
end;

initialization
  writeln('init IFDiamondA');
finalization
  writeln('final IFDiamondA');
end.
