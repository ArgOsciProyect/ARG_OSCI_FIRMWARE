def extract_12bit_values(data):
    """Extraer valores de 12 bits de un flujo de bytes."""
    values = []
    
    # Iterar sobre los datos en bloques de 3 bytes
    for i in range(0, len(data), 3):
        # Asegurarse de no exceder el tamaño de los datos
        if i + 2 >= len(data):
            break
        
        # Leer tres bytes
        byte1 = data[i]
        byte2 = data[i + 1]
        byte3 = data[i + 2]

        # Extraer los 12 bits de la primera muestra
        value1 = (byte1 << 4) | (byte2 >> 4)

        # Extraer los 12 bits de la segunda muestra
        value2 = ((byte2 & 0x0F) << 8) | byte3

        # Agregar los valores extraídos a la lista
        values.append(value1)
        values.append(value2)

    return values
