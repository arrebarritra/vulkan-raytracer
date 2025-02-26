// From https://stackoverflow.com/a/22681410
vec3 spectralColour(float l) {
	float t;
	vec3 c = vec3(0.0);

	// Red
	if ((l >= 400.0) && (l < 410.0)) { 
		t = (l - 400.0) / (410.0 - 400.0);
		c.r = +(0.33 * t) - (0.20 * t * t); 
	} else if ((l >= 410.0) && (l < 475.0)) {
		t = (l - 410.0) / (475.0 - 410.0);
		c.r = 0.14 - (0.13 * t * t); 
	} else if ((l >= 545.0) && (l < 595.0)) {
		t = (l - 545.0) / (595.0 - 545.0);
		c.r = +(1.98 * t) - (t * t); 
	} else if ((l >= 595.0) && (l < 650.0)) {
		t = (l - 595.0) / (650.0 - 595.0);
		c.r = 0.98 + (0.06 * t) - (0.40 * t * t); 
	} else if ((l >= 650.0) && (l < 700.0)) {
		t = (l - 650.0) / (700.0 - 650.0);
		c.r = 0.65 - (0.84 * t) + (0.20 * t * t); 
	}
	
	// Green
	if ((l >= 415.0) && (l < 475.0)) { 
		t = (l - 415.0) / (475.0 - 415.0);
		c.g = +(0.80 * t * t); 
	} else if ((l >= 475.0) && (l < 590.0)) {
		t = (l - 475.0) / (590.0 - 475.0);
		c.g = 0.8 + (0.76 * t) - (0.80 * t * t); 
	} else if ((l >= 585.0) && (l < 639.0)) { 
		t = (l - 585.0) / (639.0 - 585.0);
		c.g = 0.84 - (0.84 * t); 
	}

	// Blue
	if ((l >= 400.0) && (l < 475.0)) { 
		t = (l - 400.0) / (475.0 - 400.0);
		c.b = +(2.20 * t) - (1.50 * t * t); 
	} else if ((l >= 475.0) && (l < 560.0)) {
		t = (l - 475.0) / (560.0 - 475.0);
		c.b = 0.7 - (t)+(0.30 * t * t); 
	}

	return c;
}

float xFit_1931(float wave) {
	float t1 = (wave - 442.0) * ((wave < 442.0) ? 0.0624 : 0.0374);
	float t2 = (wave - 599.8) * ((wave < 599.8) ? 0.0264 : 0.0323);
	float t3 = (wave - 501.1) * ((wave < 501.1) ? 0.0490 : 0.0382);
	return 0.362 * exp(-0.5 * t1 * t1) + 1.056 * exp(-0.5 * t2 * t2)
		- 0.065 * exp(-0.5 * t3 * t3);
}

float yFit_1931(float wave) {
	float t1 = (wave - 568.8) * ((wave < 568.8) ? 0.0213 : 0.0247);
	float t2 = (wave - 530.9) * ((wave < 530.9) ? 0.0613 : 0.0322);
	return 0.821 * exp(-0.5 * t1 * t1) + 0.286 * exp(-0.5 * t2 * t2);
}

float zFit_1931(float wave) {
	float t1 = (wave - 437.0) * ((wave < 437.0) ? 0.0845 : 0.0278);
	float t2 = (wave - 459.0) * ((wave < 459.0) ? 0.0385 : 0.0725);
	return 1.217 * exp(-0.5 * t1 * t1) + 0.681 * exp(-0.5 * t2 * t2);
}

vec3 spectralColour1931(float l) {
	return mat3(2.364613, -0.5151166, 0.005203, -0.896541, 1.426408, -0.014408, -0.468073, 0.088758, 1.009204)
		* vec3(xFit_1931(l), yFit_1931(l), zFit_1931(l));
}