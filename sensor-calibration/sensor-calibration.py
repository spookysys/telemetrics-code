#!/usr/bin/env python3

import time
import json
import math
import sys

import numpy as np
from OpenGL.GL import *
from OpenGL.GLU import *
from OpenGL.GLUT import *
from transforms3d import axangles
import msgpack

import ellipsoid_fit as ellipsoid_fit_py


# settings
sample_hz = 5 # calibration data at this rate
input_precision = 2**8 # precision of input data


# visualization
draw_autorotate = True
draw_ortho = True
draw_accel_mag_cloud = 0.75
draw_gyro_cloud = False
draw_axes = True
draw_anim_accel_mag = True
draw_gyro_sticks = True # Broken, because of overflow culling



def load_input():
    if len(sys.argv) != 2:
        raise FileNotFoundError("Please specify input file as only argument")

    with open(sys.argv[1]) as jsonfile:
        input_json = json.load(jsonfile)

    accel_raw = []
    mag_raw = []
    gyro_raw = []
    for item in input_json:
        accel_raw.append(np.array(item['accel']) / input_precision)
        mag_raw.append(np.array(item['mag']) / input_precision)
        gyro_axis = np.array(item['gyro']) / input_precision
        gyro_mag = item['gyro_mag'] / input_precision
        gyro_raw.append(normalize(gyro_axis) * gyro_mag)

    return accel_raw, mag_raw, gyro_raw


def pointcloud_fit(from_pts, to_pts):
    scale = []
    center = []
    for axis_i in range(len(from_pts[0])):
        a = [[v[axis_i], 1] for v in from_pts]
        b = [v[axis_i] for v in to_pts]
        x, resid, rank, s = np.linalg.lstsq(a, b)
        scale.append(x[0])
        center.append(x[1]/x[0])

    normalize = (scale[0] * scale[1] * scale[2]) ** (1/3)

    return {
        'center': center,
        'rescale': scale / normalize,
        'normalize': normalize
    }




# return fitted value
def adjust(vec, fit):
    center = fit['center']
    rescale = fit['rescale']
    normalize = fit['normalize']
    return ((np.array(vec) - center) * rescale) * normalize



def accel_mag_fit(data):
    # Regularize
    data2 = ellipsoid_fit_py.data_regularize(np.array(data), divs=8)

    # Calculate fit
    center, radii, evecs, v = ellipsoid_fit_py.ellipsoid_fit(np.array(data2))

    # Calculate center and scale and return
    D = np.diag(1 / radii)
    TR = evecs.dot(D).dot(evecs.T)

    # Create the fit
    scale = TR.diagonal()
    normalize = (scale[0] * scale[1] * scale[2]) ** (1/3)
    fit = {
        'center': center.flatten(),
        'rescale': scale / normalize,
        'normalize': normalize
    }

    # Process the data
    fitted = [adjust(x, fit).tolist() for x in data]

    # Return the fit and the fitted data
    return (fit, fitted)




def gyro_fit(accel_fitted, mag_fitted, gyro_raw):
    # Reconstruct midpoints between recorded samples
    observed = []
    for f in range(len(gyro_raw)-1):
        observed_axis = normalize(normalize(gyro_raw[f]) + normalize(gyro_raw[f+1]))
        observed_angle = (np.linalg.norm(gyro_raw[f]) + np.linalg.norm(gyro_raw[f+1])) / 2
        observed.append((observed_axis * observed_angle).tolist())

    # Calculate expected rotation from accelerometer and magnetometer data
    calculated = []
    for f in range(len(accel_fitted)-1):
        calculated_rot_mat = rotation_from_two_vectors(
            accel_fitted[f], mag_fitted[f],
            accel_fitted[f+1], mag_fitted[f+1]
        )
        (calculated_axis, calculated_angle) = axangles.mat2axangle(calculated_rot_mat)
        calculated_angle *= sample_hz / (2*math.pi) # -> roundtrips per second
        calculated.append((calculated_axis * calculated_angle).tolist())

    # Cull datapoints that might have overflowed the gyro,
    # so they don't participate in calculating the fit
    calculated_culled = []
    observed_culled = []
    used = []
    for i in range(len(calculated)):
        th = 0.9 # threshold for culling
        p = abs(np.array(calculated[i]))
        if p[0] < th and p[1] < th and p[2] < th:
            calculated_culled.append(calculated[i])
            observed_culled.append(observed[i])
            used.append(True)
        else:
            used.append(False)

    # Print a stat about the culling
    print("Gyro points culled by overflow protection: ", len(calculated)-len(calculated_culled))

    # Calculate fitting
    fit = pointcloud_fit(observed_culled, calculated_culled)

    # Fit the data
    fitted_culled = [adjust(vec, fit).tolist() for vec in observed_culled]
    fitted = [adjust(vec, fit).tolist() for vec in observed]

    # Return
    return (fit, fitted, calculated, used)





def normalize(v):
    norm = np.linalg.norm(v)
    if norm == 0:
        return v
    return v / norm


# Calculate current rotation
def rotation_from_two_vectors(t0v0, t0v1, t1v0, t1v1):
    t0v0 = normalize(t0v0)
    t0v1 = normalize(t0v1)
    t0a0 = normalize(t0v0+t0v1)
    t0a1 = normalize(np.cross(t0v0, t0v1))
    t0a2 = normalize(np.cross(t0a0, t0a1))
    t0mat = np.mat([t0a0.tolist(), t0a1.tolist(), t0a2.tolist()])

    t1v0 = normalize(t1v0)
    t1v1 = normalize(t1v1)
    t1a0 = normalize(t1v0+t1v1)
    t1a1 = normalize(np.cross(t1v0, t1v1))
    t1a2 = normalize(np.cross(t1a0, t1a1))
    t1mat = np.mat([t1a0.tolist(), t1a1.tolist(), t1a2.tolist()])

    return t0mat.T * t1mat

# Load inputs
accel_raw, mag_raw, gyro_raw = load_input()

# Fit everything
(accel_fit, accel_fitted) = accel_mag_fit(accel_raw)
(mag_fit, mag_fitted) = accel_mag_fit(mag_raw)
(gyro_fit, gyro_fitted, gyro_calculated, gyro_used) = gyro_fit(accel_fitted, mag_fitted, gyro_raw)

############################################
## PRINT OUTPUT
############################################

output = {
    'accel': accel_fit,
    'mag': mag_fit,
    'gyro': gyro_fit
}

print(output)




############################################
## EVERYTHING BELOW IS VISUALIZATION CODE
############################################


gl_anim = 0
gl_view_rotate = np.array([0, 0])
gl_eye_distance = 5
gl_start_time = time.time()
gl_paused = False
def gl_display():
    # Get frame and delta for animation
    if gl_paused:
        t = gl_start_time
    else:
        t = time.time() - gl_start_time
    t *= 4
    d = t % 1
    f = int(t) % (len(accel_raw)-1)
    accel_vec = (1-d)*np.array(accel_fitted[f]) + d*np.array(accel_fitted[f+1])
    mag_vec = (1-d)*np.array(mag_fitted[f]) + d*np.array(mag_fitted[f+1])
    gyro_vec = (1-d)*np.array(gyro_raw[f]) + d*np.array(gyro_raw[f+1])

    # Calculate axes
    axes = [
        normalize(normalize(accel_vec) + normalize(mag_vec)),
        normalize(np.cross(normalize(accel_vec), normalize(mag_vec))),
        None
    ]
    axes[2] = normalize(np.cross(axes[0], axes[1]))


    # Prepare for drawing
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT)
    glEnable(GL_BLEND)
    glBlendFunc(GL_SRC_ALPHA, GL_ONE)
    glPushMatrix()

    gluLookAt(0, 0, gl_eye_distance,
              0, 0, 0,
              0, 1, 0)


    scaler = 2
    glRotate(gl_view_rotate[1]/scaler, 1, 0, 0)
    glRotate(gl_view_rotate[0]/scaler, 0, 1, 0)

    if draw_autorotate:
        rotMat = [axes[0][0], axes[1][0], axes[2][0], 0., axes[0][1], axes[1][1], axes[2][1], 0., axes[0][2], axes[1][2], axes[2][2], 0., 0., 0., 0., 1.]
        glMultMatrixd(rotMat)

    # Draw unit cube
    glLineWidth(1)
    glColor3fv([1, 1, 1])
    glutWireCube(2)

    # Draw mag and accel strips
    if draw_accel_mag_cloud > 0:
        for i in range(2):
            alpha = draw_accel_mag_cloud
            glPointSize(3)
            glLineWidth(1)
            glColor([[1, 0, 1, alpha], [0, 1, 0, alpha]][i])
            glBegin(GL_POINTS)
            for p in [accel_fitted, mag_fitted][i]:
                glVertex(p)
            glEnd()

    # Draw gyro clouds
    if draw_gyro_cloud:
        alpha = 0.5
        scale = 1

        c = [[1, 0, 1, alpha], [0, 1, 0, alpha], [.33, .33, .33, alpha*.33]]
        v0 = np.array(gyro_calculated) * scale
        v1 = np.array(gyro_fitted) * scale

        glShadeModel(GL_SMOOTH)
        glLineWidth(1)
        glBegin(GL_LINES)
        for i in range(len(gyro_fitted)):
            if gyro_used[i]:
                glColor(c[0])
                glVertex(v0[i])
                glColor(c[1])
                glVertex(v1[i])
            else:
                glColor(c[2])
                glVertex(v0[i])
                glColor(c[2])
                glVertex(v1[i])
        glEnd()

        glShadeModel(GL_FLAT)
        glPointSize(3)
        glBegin(GL_POINTS)
        for i in range(len(gyro_fitted)):
            if gyro_used[i]:
                glColor(c[0])
                glVertex(v0[i])
                glColor(c[1])
                glVertex(v1[i])
        glEnd()


    if draw_anim_accel_mag:
        glLineWidth(1)
        glBegin(GL_LINES)
        glColor(1, 0, 1)
        glVertex(0, 0, 0)
        glVertex(accel_vec)
        glColor(0, 1, 0)
        glVertex(0, 0, 0)
        glVertex(mag_vec)
        glEnd()

    if draw_axes:
        glLineWidth(4)
        glBegin(GL_LINES)
        glColor(1, 1, 0)
        glVertex(0, 0, 0)
        glVertex(axes[0])
        glColor(1, 0, 0)
        glVertex(0, 0, 0)
        glVertex(axes[1])
        glColor(0, 0, 1)
        glVertex(0, 0, 0)
        glVertex(axes[2])
        glEnd()

    if draw_gyro_sticks:
        scale = 3
        glLineWidth(4)
        glBegin(GL_LINES)
        glColor(1, 1, .5, 1)
        glVertex(0, 0, 0)
        glVertex(np.array(gyro_calculated[f]) * scale)
        glColor(1, .5, 1, 1)
        glVertex(0, 0, 0)
        glVertex(np.array(gyro_fitted[f]) * scale)
        glEnd()


    glPopMatrix()
    glutSwapBuffers()
    return

gl_mouse_last_coord = [0, 0]
def gl_mouse_button(button, state, x, y):
    global gl_paused, gl_start_time
    global gl_mouse_last_coord
    if state == 0 and button == 2:
        if not gl_paused:
            gl_paused = True
            gl_start_time = time.time() - gl_start_time
        else:
            gl_paused = False
            gl_start_time = time.time() - gl_start_time

    gl_mouse_last_coord = [x, y]

def gl_mouse_motion(x, y):
    global gl_view_rotate, gl_mouse_last_coord
    gl_view_rotate += np.array([x, y]) - gl_mouse_last_coord
    gl_mouse_last_coord = [x, y]

def gl_idle():
    glutPostRedisplay()

def gl_main():
    glutInit(sys.argv)
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH)
    glutInitWindowSize(800, 800)
    glutCreateWindow(b"Visualize")

    glClearColor(0., 0., 0., 1.)

    glShadeModel(GL_FLAT)
    glEnable(GL_CULL_FACE)
    glDisable(GL_DEPTH_TEST)

    glFogi(GL_FOG_MODE, GL_LINEAR)
    glFogfv(GL_FOG_COLOR, [0, 0, 0])
    glFogf(GL_FOG_DENSITY, 1.0)
    glFogf(GL_FOG_START, gl_eye_distance-1) # Fog Start Depth
    glFogf(GL_FOG_END, gl_eye_distance+1) # Fog End Depth
    glEnable(GL_FOG)

    glutDisplayFunc(gl_display)
    glutMotionFunc(gl_mouse_motion)
    glutMouseFunc(gl_mouse_button)
    glutIdleFunc(gl_idle)
    glMatrixMode(GL_PROJECTION)
    if draw_ortho:
        glOrtho(-2, 2, -2, 2, 1, gl_eye_distance * 2)
    else:
        gluPerspective(40., 1., 1., 40.)
    glMatrixMode(GL_MODELVIEW)
    glutMainLoop()
    return


if __name__ == '__main__':
    gl_main()
