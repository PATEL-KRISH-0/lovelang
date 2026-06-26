# benchmark/bench.py — sum loop 0..20,000,000
n = 20000000
i = 0
s = 0
while i < n:
    s += i
    i += 1
print(s)
