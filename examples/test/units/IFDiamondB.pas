unit IFDiamondB;

interface

uses IFBase;

function GetB: integer;

implementation

function GetB;
begin
  GetB := BaseVal + 2;
end;

initialization
  writeln('init IFDiamondB');
finalization
  writeln('final IFDiamondB');
end.
