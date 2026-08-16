program TestDirectivesNested;
{$DEFINE OUTER}
begin
{$IFDEF OUTER}
    writeln('outer true');
    {$IFDEF INNER}
        writeln('should not print - inner false');
    {$ELSE}
        writeln('inner false, correctly reached');
    {$ENDIF}
{$ENDIF}
{$IFDEF NOPE_OUTER}
    {$IFDEF DEBUG}
        writeln('should never print - outer false suppresses inner regardless');
    {$ENDIF}
{$ENDIF}
    writeln('done');
end.
