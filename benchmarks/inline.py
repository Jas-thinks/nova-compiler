def sq(x): return x * x
def mix(a, b): return sq(a % 1000) + b * 7
def clamp(x, hi): return hi if x > hi else x
acc = 0
for i in range(2000000):
    acc = clamp(acc + mix(i, acc), 1000000007)
print(acc)
