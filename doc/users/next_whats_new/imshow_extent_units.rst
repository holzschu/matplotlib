The *extent* of ``imshow`` can now be expressed with units
----------------------------------------------------------
The *extent* parameter of `~.axes.Axes.imshow` and  `~.AxesImage.set_extent`
can now be expressed with units.

.. plot::
    :include-source: true

    import matplotlib.pyplot as plt
    import numpy as np
    from matplotlib.dates import HourLocator, DateFormatter
    from matplotlib.ticker import AutoMinorLocator

    fig, ax = plt.subplots()
    date_first = np.datetime64('2020-01-01', 'D')
    date_last = np.datetime64('2020-01-11', 'D')

    arr = [[i+j for i in range(10)] for j in range(10)]

    ax.imshow(arr, origin='lower', extent=[1, 11, date_first, date_last])

    ax.yaxis.set_major_formatter(DateFormatter('%d/%m/%y:- %H00hours'))
    ax.yaxis.set_major_locator(HourLocator(byhour=[0, 6, 12, 18, 24]))
    ax.yaxis.set_minor_locator(AutoMinorLocator())

    plt.show()
