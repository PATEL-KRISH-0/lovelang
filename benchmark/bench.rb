# benchmark/bench.rb — sum loop 0..20,000,000
n = 20000000
i = 0
sum = 0
while i < n
  sum += i
  i += 1
end
puts sum
