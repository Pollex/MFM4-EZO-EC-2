import numpy as np
from scipy.stats import linregress
import matplotlib.pyplot as plt

# Temperature (°C) and corresponding conductivity (µS/cm) values from the table
t_low = np.array([5, 10, 15, 20, 25, 30, 35, 40, 45, 50])  # °C
c_low = np.array([8220, 9330, 10480, 11670, 12880, 14120, 15550, 16880, 18210, 19550])  # µS/cm
t_high = np.array([5, 10, 15, 20, 25, 30, 35, 40, 45, 50])  # °C
c_high = np.array([53500, 59600, 65400, 72400, 80000, 88200, 96400, 104600, 112800, 121000])  # µS/cm


def create_linear_formula(t, c):
    # Perform linear regression to find the best fit line
    slope, intercept, r_value, p_value, std_err = linregress(t, c)
# Define the formula
    return lambda temp: slope * temp + intercept


def create_poly_formula(t, c, order=3):
    orders = np.polyfit(t, c, order)
    return np.poly1d(orders), orders


def plot_formula(t, c, formula, title=None):
    # Generate temperatures for plotting the regression line
    temp_range = np.linspace(5, 50, 100)
    # predicted values
    predicted_conductivities = formula(temp_range)
    # offsets
    offsets = calculate_off(t, c, formula)
    # Plotting the data points and the regression line
    plt.figure(figsize=(10, 6))
    plt.scatter(t, c, color='blue', label='Data Points')
    plt.plot(temp_range, predicted_conductivities, color='red', label='Linear Regression Line')
    # plt.scatter(25, formula(25), color='green', s=80, label='Ref')
    # Labels and title
    plt.xlabel('Temperature (°C)')
    plt.ylabel('Conductivity (µS/cm)')
    plt.title(f'Extrapolated Temperature vs Conductivity{"\n" + title}\n{str(formula)} ; Error={offsets:.0f}')
    plt.legend()
    plt.grid(True)
    plt.tight_layout()
    if title is not None:
        plt.savefig(f"docs/{title.lower().replace(' ', '_')}.svg")
        plt.savefig(f"docs/{title.lower().replace(' ', '_')}.png")
    plt.show()


def calculate_off(t, c, formula):
    c_cal = formula(t)
    return np.mean(np.sqrt(np.power((c_cal - c), 2)))


# lin_low = create_linear_formula(t_low, c_low)
# plot_formula(t_low, c_low, lin_low, "Calibration Extrapolation Linear Low")

# lin_high = create_linear_formula(t_high, c_high)
# plot_formula(t_high, c_high, lin_high, "Calibration Extrapolation Linear High")

poly_low, orders_low = create_poly_formula(t_low, c_low, 3)
# plot_formula(t_low, c_low, poly_low, "Calibration Extrapolation Poly Low")

poly_high, orders_high = create_poly_formula(t_high, c_high, 3)
# plot_formula(t_high, c_high, poly_high, "Calibration Extrapolation Poly High")

temps = np.arange(0,30.1, 0.1)
lows = poly_low(temps)
highs = poly_high(temps)

with open("calibrations.csv", "w") as file:
    import csv
    writer = csv.writer(file)
    writer.writerow(["Temperature", "Low uS", "High uS"])
    for i in range(len(temps)):
        writer.writerow([
            f"{temps[i]:.3f}",
            f"{lows[i]:.3f}",
            f"{highs[i]:.3f}",
        ])

# temp = 23.2
# print(poly_low(temp), poly_high(temp))
