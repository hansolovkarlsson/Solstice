unit DirUnit;
interface
procedure Report;
implementation
procedure Report;
begin
{$IFDEF FROM_MAIN}
    writeln('unit sees FROM_MAIN');
{$ELSE}
    writeln('unit does NOT see FROM_MAIN');
{$ENDIF}
end;
end.
