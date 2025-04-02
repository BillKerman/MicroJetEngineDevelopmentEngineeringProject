import math as m

cp = 1001
rc = 3
T_in = 300
gamma = 1.402
beta1 = 25
beta2 = 30
rho_T_in = 1.17605
rho_T_0 = 0.7
N = 35000
dim_rat = 1.0/2.27838  #dimention ratio between 
r1 = (1.4/2)*(0.0254)*(dim_rat)
r2 = (8.97/2)*(0.0254)*(dim_rat)
rd = 0.15
t1 = (0.02)*(0.0254)*(dim_rat)
t2 = (0.84)*(0.0254)*(dim_rat)
b1 = (2.67)*(0.0254)*(dim_rat)
b2 = (0.48)*(0.0254)*(dim_rat)
n1 = 11
n2 = 22


v1 = N*r1
v2 = N*r2 

V_w2 = 0
slip = (N*2*r2/2)-V_w2
#slip_factor = V_w2/(N*2*r2/2)
slip_factor = 0.85 #This is the general value of slip factor in practical sinarios
m_dot = (1-slip_factor)*rho_T_0*(2*m.pi*r2-n2*t1)*b2*(2*m.pi*N*r2*m.tan(m.pi/6))/60
#vw2 = v2 - vr2*m.cos(beta1)
Torque = m_dot*v2*2*r2/2
power = Torque*2*m.pi*(N/60)
work = cp*T_in*(pow(rc,((gamma-1)/gamma))-1)


print("Power : "+str(power) + " Watt")
print("Work : "+str(work) + " Joules")
print("Torque : "+str(Torque) + " N-m")
print("mass flow rate : "+str(m_dot) + " Kg/s")

