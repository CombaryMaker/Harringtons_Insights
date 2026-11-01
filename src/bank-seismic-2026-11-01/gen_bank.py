# gen_bank.py - generates SYNTHETIC bank data in public formats (no real people, accounts or banks).
# Michael Hugh Harrington, 2026. MIT License.
#   ach/*.ach  NACHA ACH files (published 94-character record layout): payroll and bill-payment batches
#   ej/*.txt   ATM electronic journals (the text log an ATM prints for every transaction and device event)
# Routing numbers are random with a valid ABA check digit; names are random pairs from common-name lists.
import random, os, datetime
random.seed(2026)
FIRST = 'JAMES MARY ROBERT PATRICIA JOHN JENNIFER MICHAEL LINDA DAVID ELIZABETH WILLIAM BARBARA RICHARD SUSAN JOSEPH JESSICA THOMAS SARAH CHARLES KAREN CHRISTOPHER LISA DANIEL NANCY MATTHEW BETTY ANTHONY SANDRA MARK MARGARET DONALD ASHLEY STEVEN KIMBERLY PAUL EMILY ANDREW DONNA JOSHUA MICHELLE KENNETH CAROL KEVIN AMANDA BRIAN MELISSA GEORGE DEBORAH TIMOTHY STEPHANIE RONALD REBECCA EDWARD SHARON JASON LAURA JEFFREY CYNTHIA RYAN DOROTHY JACOB AMY GARY KATHLEEN NICHOLAS ANGELA ERIC SHIRLEY JONATHAN BRENDA STEPHEN EMMA LARRY ANNA JUSTIN PAMELA SCOTT NICOLE BRANDON SAMANTHA BENJAMIN KATHERINE SAMUEL CHRISTINE GREGORY HELEN ALEXANDER DEBRA FRANK RACHEL PATRICK CAROLYN RAYMOND JANET JACK MARIA DENNIS CATHERINE JERRY HEATHER'.split()
LAST = 'SMITH JOHNSON WILLIAMS BROWN JONES GARCIA MILLER DAVIS RODRIGUEZ MARTINEZ HERNANDEZ LOPEZ GONZALEZ WILSON ANDERSON THOMAS TAYLOR MOORE JACKSON MARTIN LEE PEREZ THOMPSON WHITE HARRIS SANCHEZ CLARK RAMIREZ LEWIS ROBINSON WALKER YOUNG ALLEN KING WRIGHT SCOTT TORRES NGUYEN HILL FLORES GREEN ADAMS NELSON BAKER HALL RIVERA CAMPBELL MITCHELL CARTER ROBERTS GOMEZ PHILLIPS EVANS TURNER DIAZ PARKER CRUZ EDWARDS COLLINS REYES STEWART MORRIS MORALES MURPHY COOK ROGERS GUTIERREZ ORTIZ MORGAN COOPER PETERSON BAILEY REED KELLY HOWARD RAMOS KIM COX WARD RICHARDSON WATSON BROOKS CHAVEZ WOOD JAMES BENNETT GRAY MENDOZA RUIZ HUGHES PRICE ALVAREZ CASTILLO SANDERS PATEL MYERS LONG ROSS FOSTER'.split()
def aba():
    d = [random.randint(0, 9) for _ in range(8)]; w = [3, 7, 1, 3, 7, 1, 3, 7]
    return ''.join(map(str, d)) + str((10 - sum(a * b for a, b in zip(d, w)) % 10) % 10)
BANKS = [aba() for _ in range(400)]
def name(): return f'{random.choice(FIRST)} {random.choice(LAST)}'
def ach_file(day, fid):
    recs = []; odfi = random.choice(BANKS); comp = [(f'EMPLOYER {i:03d} INC', f'1{random.randint(10**8, 10**9-1)}') for i in range(random.randint(3, 12))]
    recs.append(f'101 {random.choice(BANKS)}{"1"+odfi}{day:%y%m%d}{random.randint(0,23):02d}{random.randint(0,59):02d}{fid}094101{"FEDERAL RESERVE BANK":<23}{"FIRST SAMPLE BANK":<23}{"":<8}')
    tot_e = tot_h = tot_d = tot_c = 0
    for b, (cname, cid) in enumerate(comp, 1):
        sec, desc, cls = random.choice([('PPD', 'PAYROLL', 220), ('PPD', 'PAYROLL', 220), ('CCD', 'VENDOR PAY', 220), ('PPD', 'UTILITY', 225)])
        recs.append(f'5{cls}{cname[:16]:<16}{"":<20}{cid}{sec}{desc:<10}{day:%y%m%d}{day:%y%m%d}   1{odfi[:8]}{b:07d}')
        n = random.randint(20, 900); bh = bd = bc = 0
        for s in range(n):
            rdfi = random.choice(BANKS); tc = (27 if cls == 225 else random.choice([22, 22, 22, 32]))
            amt = int(random.lognormvariate(11.0 if cls == 220 else 9.0, 0.6)); add = random.random() < 0.08
            recs.append(f'6{tc}{rdfi}{str(random.randint(10**6, 10**12)):<17}{amt:010d}{str(random.randint(10**5, 10**9)):<15}{name()[:22]:<22}  {1 if add else 0}{odfi[:8]}{s+1:07d}')
            if add: recs.append(f'705{"REF*TN*" + str(random.randint(10**6, 10**9)) + "\\":<80}0001{s+1:07d}')
            bh += int(rdfi[:8]); bd += amt if tc == 27 else 0; bc += amt if tc != 27 else 0; tot_e += 1 + add
        recs.append(f'8{cls}{n:06d}{bh % 10**10:010d}{bd:012d}{bc:012d}{cid}{"":<19}{"":<6}{odfi[:8]}{b:07d}')
        tot_h += bh; tot_d += bd; tot_c += bc
    blocks = (len(recs) + 1 + 9) // 10
    recs.append(f'9{len(comp):06d}{blocks:06d}{tot_e:08d}{tot_h % 10**10:010d}{tot_d:012d}{tot_c:012d}{"":<39}')
    while len(recs) % 10: recs.append('9' * 94)
    assert all(len(r) == 94 for r in recs), [r for r in recs if len(r) != 94][:1]
    return '\n'.join(recs) + '\n'
def ej(day, atm):
    out = []; t = datetime.datetime.combine(day, datetime.time(0, 0)); seq = random.randint(1000, 9000)
    cash = [2000, 2000, 1500, 1000]
    while True:
        t += datetime.timedelta(seconds=int(random.expovariate(1 / 140)))
        if t.date() != day: break
        ts = f'{t:%m/%d/%y %H:%M:%S}'
        r = random.random()
        if r < 0.05:
            ev = random.choice(['CASSETTE 2 LOW', 'RECEIPT PAPER LOW', 'SUPERVISOR MODE ENTRY', 'SUPERVISOR MODE EXIT', f'COMMS ERROR {random.randint(1,255):#04x} RETRY OK',
                                'DOOR OPEN SAFE', 'DOOR CLOSED SAFE', f'CASH COUNT 1:{cash[0]} 2:{cash[1]} 3:{cash[2]} 4:{cash[3]}', 'CARD READER FAULT CLEARED'])
            out.append(f'{ts} ATM {atm:06d} EVENT {ev}')
            continue
        seq += 1; card = f'************{random.randint(0, 9999):04d}'
        kind = random.choices(['WITHDRAWAL FROM CHECKING', 'WITHDRAWAL FROM SAVINGS', 'BALANCE INQUIRY', 'DEPOSIT TO CHECKING', 'TRANSFER SAV TO CHK'], [60, 10, 18, 9, 3])[0]
        out += [f'{ts} ATM {atm:06d} TRN {seq:05d}', '*** TRANSACTION START ***', 'CARD INSERTED', f'CARD: {card}', 'PIN ENTERED', kind]
        if kind.startswith('WITHDRAWAL'):
            amt = random.choice([20, 40, 60, 80, 100, 100, 120, 140, 160, 200, 200, 300, 400])
            ok = random.random() > 0.04
            out += [f'AMOUNT ${amt:>9.2f}', f'DISPENSED  NOTES: 20x{amt // 20}' if ok else 'NOT DISPENSED',
                    f'RESPONSE: {"000 APPROVED" if ok else random.choice(["051 INSUFFICIENT FUNDS", "055 INCORRECT PIN", "061 EXCEEDS LIMIT"])}']
            if ok: cash[1] = max(0, cash[1] - amt // 20)
        elif kind.startswith('DEPOSIT'):
            out += [f'DEPOSIT ${random.randint(20, 3000):>9.2f}', f'ITEMS: {random.randint(1, 6)} CHECKS', 'RESPONSE: 000 APPROVED']
        elif kind.startswith('TRANSFER'):
            out += [f'AMOUNT ${random.randint(10, 2000):>9.2f}', 'RESPONSE: 000 APPROVED']
        else:
            out += [f'AVAIL BAL ${random.randint(0, 20000):>9.2f}', 'RESPONSE: 000 APPROVED']
        out += [f'SEQ {seq:05d}  AUTH {random.randint(100000, 999999)}', 'CARD RETURNED', '*** TRANSACTION END ***']
    return '\n'.join(out) + '\n'
os.makedirs('ach', exist_ok=True); os.makedirs('ej', exist_ok=True)
day0 = datetime.date(2026, 9, 1); total_ach = total_ej = 0
for d in range(30):
    day = day0 + datetime.timedelta(days=d)
    for k in range(6):
        s = ach_file(day, 'ABCDEF'[k]); open(f'ach/{day:%Y%m%d}_{k}.ach', 'w').write(s); total_ach += len(s)
    for a in range(12):
        s = ej(day, 4500 + a); open(f'ej/{day:%Y%m%d}_atm{4500+a}.txt', 'w').write(s); total_ej += len(s)
print(f'ACH {total_ach:,} bytes, EJ {total_ej:,} bytes')
