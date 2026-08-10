program TestClassmemberSubrange;
type
    TPercent = 0..100;
    TStats = class
    public
        class var Score: TPercent;
    end;

begin
    TStats.Score := 50;
    writeln('Score = ', TStats.Score);
end.
