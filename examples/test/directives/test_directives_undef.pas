program TestDirectivesUndef;
{$DEFINE X}
{$UNDEF X}
begin
{$IFDEF X}
    writeln('should not print');
{$ELSE}
    writeln('X undefined now');
{$ENDIF}
end.
