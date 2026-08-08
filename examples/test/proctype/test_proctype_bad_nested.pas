program TestProctypeBadNested;
type
    TIntProc = procedure(x: integer);
var
    p: TIntProc;

procedure Outer;
    procedure Inner(x: integer);
    begin
    end;
begin
    { Expected: Compile Error - only a top-level procedure/function can
      be assigned to a procedural type }
    p := Inner;
end;

begin
    Outer;
end.
