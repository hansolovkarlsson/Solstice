unit VisLib;

interface

var
  PublicCounter: integer;

function DoWork(x: integer): integer;

implementation

var
  PrivateCounter: integer;

procedure PrivateHelper;
begin
  PrivateCounter := PrivateCounter + 1;
end;

function DoWork;
begin
  PrivateHelper;
  PublicCounter := PublicCounter + 1;
  DoWork := x * 2 + PrivateCounter;
end;

end.
