program TestDirectivesUnrecognized;
{$R+}
begin
    writeln('R+ accepted and ignored');
{$Q-}
    writeln('Q- accepted and ignored');
end.
