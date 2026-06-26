#!/usr/bin/perl
# benchmark/bench.pl — sum loop 0..20,000,000
my ($n, $i, $sum) = (20000000, 0, 0);
while ($i < $n) { $sum += $i; $i++; }
print "$sum\n";
