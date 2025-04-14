import pandas as pd

# Defining the data in tabular format for easier processing
data = {
    'Temperature': [24.0, 24.0, 24.0, 24.0, 24.0, 23.2, 23.2, 23.2, 23.2, 23.2],
    'Sensor': ['Ref', '001 A', '001 B', '002 A', '002 B', 'Ref', '003 A', '003 B', '004 A', '004 B'],

    'Custom Reading': [13.19, 12.86, 12.70, 12.62, 12.40, 13.18, 12.07, 12.17, 12.28, 12.28],
    'Low Reading': [13.38, 12.98, 12.86, 12.75, 12.62, 13.46, 12.37, 12.40, 12.56, 12.55],
    'High Reading': [81.70, 81.74, 80.47, 79.25, 82.02, 81.80, 75.30, 76.96, 77.36, 77.89],

    'Custom Low': [12.9] * 10,
    'Calibrated Low': [12.802] * 5 + [12.449] * 5,
    'Calibrated High': [80.061] * 5 + [77.343] * 5,
}

# Creating DataFrame
df = pd.DataFrame(data)

# Calculate Deviation from Expected Value
df['Custom Deviation'] = df['Custom Reading'] - df['Custom Low']
df['Low Deviation'] = df['Low Reading'] - df['Calibrated Low']
df['High Deviation'] = df['High Reading'] - df['Calibrated High']

# Calculate Absolute Deviations
df['Low Abs Deviation'] = df['Low Deviation'].abs()
df['High Abs Deviation'] = df['High Deviation'].abs()

# Calculating Mean Absolute Deviation (MAD)
low_mad = df['Low Abs Deviation'].mean()
high_mad = df['High Abs Deviation'].mean()

# Calculating Standard Deviations (Intra-Sensor Consistency)
low_std_dev = df.groupby('Sensor')['Low Reading'].std().fillna(0)
high_std_dev = df.groupby('Sensor')['High Reading'].std().fillna(0)


# Packing all results into a dictionary for display
results = {
    "Mean Absolute Deviation (Low)": low_mad,
    "Mean Absolute Deviation (High)": high_mad,
    "Standard Deviation by Sensor (Low)": low_std_dev,
    "Standard Deviation by Sensor (High)": high_std_dev,
}

results

df[['Sensor', 'Custom Deviation', 'Low Deviation', 'High Deviation']].describe()
