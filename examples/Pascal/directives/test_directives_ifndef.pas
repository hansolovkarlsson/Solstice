program TestDirectivesIfndef;
begin
{$IFNDEF NOPE}
    writeln('NOPE is not defined');
{$ENDIF}
end.
