program TestDirectivesBasic;
{$DEFINE DEBUG}
begin
{$IFDEF DEBUG}
    writeln('debug on');
{$ENDIF}
end.
