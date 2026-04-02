import * as m from 'zigbee-herdsman-converters/lib/modernExtend';

export default {
    zigbeeModel: ['nordic DIY'],
    model: 'nordic DIY',
    vendor: 'Nordic',
    description: 'Nordic DIY sensor',
    extend: [
        m.temperature({
            reporting: {
                min: 60,
                max: 300,
                change: 10,
            },
        }),
	m.humidity({
            reporting: {
                min: 60,
                max: 300,
                change: 10,
            },
        }),
	m.pressure({
            reporting: {
                min: 60,
                max: 300,
                change: 1,
            },
	    unit: 'hPa',
        }),
        m.battery({
            percentageReportingConfig: {
                min: 600,
                max: 3600,
                change: 2,
            },
        }),
    ],
};