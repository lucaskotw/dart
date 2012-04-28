#include "MyWindow.h"
#include "dynamics/BodyNodeDynamics.h"
#include "dynamics/ContactDynamics.h"
#include "kinematics/Dof.h"
#include "utils/UtilsMath.h"
#include "utils/Timer.h"
#include "yui/GLFuncs.h"
#include <cstdio>
#include "kinematics/BodyNode.h"

using namespace Eigen;
using namespace kinematics;
using namespace utils;
using namespace integration;
using namespace dynamics;

void MyWindow::initDyn()
{
    mDofs.resize(mSkels.size());
    mDofVels.resize(mSkels.size());

    for (unsigned int i = 0; i < mSkels.size(); i++) {
        mDofs[i].resize(mSkels[i]->getNumDofs());
        mDofVels[i].resize(mSkels[i]->getNumDofs());
        mDofs[i].setZero();
        mDofVels[i].setZero();
    }

    mDofs[0][1] = -0.35;
    mDofs[2][0] = 0.1;
    mDofs[2][1] = -0.324;
    mDofs[1][0] = -0.1;
    mDofs[1][1] = -0.3;
    mDofs[1][2] = 0.1;
    mDofs[1][4] = 1.57;
    mDofs[1][7] = -1.3;
    mDofs[1][8] = -1.3;
    mDofs[1][9] = 0.7;
    mDofs[1][10] = -0.3;
    mDofs[1][11] = -0.3;

    for (unsigned int i = 0; i < mSkels.size(); i++) {
        mSkels[i]->setPose(mDofs[i], false, false);
        mSkels[i]->computeDynamics(mGravity, mDofVels[i], false);
    }
    mSkels[0]->setImmobileState(true);
    //    mSkels[1]->setKinematicState(true);
    mSkels[1]->setHybridState(true);

    mCollisionHandle = new dynamics::ContactDynamics(mSkels, mTimeStep);
}

VectorXd MyWindow::getState() {
    VectorXd state(mIndices.back() * 2);    
    for (unsigned int i = 0; i < mSkels.size(); i++) {
        int start = mIndices[i] * 2;
        int size = mDofs[i].size();
        state.segment(start, size) = mDofs[i];
        state.segment(start + size, size) = mDofVels[i];
    }
    return state;
}

VectorXd MyWindow::evalDeriv() {
    setPose();
    VectorXd rootAccel(6);
    rootAccel.setZero();
    rootAccel[0]= 0.01;
    VectorXd deriv = VectorXd::Zero(mIndices.back() * 2);    
    for (unsigned int i = 0; i < mSkels.size(); i++) {
        if (mSkels[i]->getImmobileState() || mSkels[i]->getKinematicState())
            continue;
        int start = mIndices[i] * 2;
        int size = mDofs[i].size();
        VectorXd qddot;
        if (mSkels[i]->getHybridState() ){
            int nDof = mSkels[i]->getNumDofs();
            VectorXd globalInertia = mSkels[i]->getMassMatrix().block(0, 0, nDof, 6) * rootAccel;
            MatrixXd assembledM(nDof, nDof);
            assembledM.block(0, 0, 6, 6) = MatrixXd::Identity(6, 6);
            assembledM.block(6, 0, nDof - 6, 6) = MatrixXd::Zero(nDof - 6, 6);
            assembledM.block(0, 6, nDof, nDof - 6) = mSkels[i]->getMassMatrix().block(0, 6, nDof, nDof - 6);
            VectorXd torque(nDof);
            torque.setZero();
            for (int j = 0; j < mSkels[i]->getNumNodes(); j++) {
                BodyNodeDynamics *node = static_cast<BodyNodeDynamics*>(mSkels[i]->getNode(j));
                node->aggregateGravity(torque);
            }
            //            VectorXd torque = assembledM.fullPivHouseholderQr().solve(-mSkels[i]->getCombinedVector()  - globalInertia);
            //torque.setZero();
            //            torque[1] = mSkels[i]->getMass() * 9.8;
            //           torque.segment(6, nDof - 6).setZero();
            //cout << torque << endl;
            qddot = mSkels[i]->getMassMatrix().fullPivHouseholderQr().solve(-mSkels[i]->getCombinedVector() + mSkels[i]->getExternalForces() + torque);
            cout << -mSkels[i]->getCombinedVector() + mSkels[i]->getExternalForces() + torque << endl;
        //cout << qddot << endl;
            //            qddot.segment(0, 6) = rootAccel;
        } else {
            qddot = mSkels[i]->getMassMatrix().fullPivHouseholderQr().solve(-mSkels[i]->getCombinedVector() + mSkels[i]->getExternalForces() + mCollisionHandle->getConstraintForce(i));
        }
        mSkels[i]->clampRotation(mDofs[i], mDofVels[i]);
        deriv.segment(start, size) = mDofVels[i] + (qddot * mTimeStep); // set velocities
        deriv.segment(start + size, size) = qddot; // set qddot (accelerations)
    }        
    return deriv;
}

void MyWindow::setState(VectorXd newState) {
    for (unsigned int i = 0; i < mSkels.size(); i++) {
        int start = mIndices[i] * 2;
        int size = mDofs[i].size();
        mDofs[i] = newState.segment(start, size);
        mDofVels[i] = newState.segment(start + size, size);
    }
}
//if mSkels[i] is kinematic, mDofs[i] and mDofVels[i] need to be updated separately. Furthermore, we need to recompute M and C.

void MyWindow::setPose() {
    for (unsigned int i = 0; i < mSkels.size(); i++) {
        if (mSkels[i]->getImmobileState()) {
            mSkels[i]->setPose(mDofs[i], true, false);
        } else {
            mSkels[i]->setPose(mDofs[i], true, true);
            mSkels[i]->computeDynamics(mGravity, mDofVels[i], true);    
        }
    }
    mCollisionHandle->applyContactForces();
}

void MyWindow::displayTimer(int _val)
{
    int numIter = mDisplayTimeout / (mTimeStep * 1000);
    if (mPlay) {
        mPlayFrame += 16;
        if (mPlayFrame >= mBakedStates.size())
            mPlayFrame = 0;
        glutPostRedisplay();
        glutTimerFunc(mDisplayTimeout, refreshTimer, _val);        
    }else if (mSim) {
        //        static Timer tSim("Simulation");
        for (int i = 0; i < numIter; i++) {
            cout << "#### Iter " << i + mSimFrame << endl;
            //            tSim.startTimer();
            //            static_cast<BodyNodeDynamics*>(mSkels[1]->getNode(0))->addExtForce(Vector3d(0.0, -0.1, 0.0), mForce);
            mIntegrator.integrate(this, mTimeStep);
            //            tSim.stopTimer();
            //            tSim.printScreen();

            /*
            if(mSimFrame > numIter * 25 && mSimFrame < numIter * 45) {    
                mDofs[1][0] += 0.005 / numIter;
                mDofs[1][2] -= 0.005 / numIter;
                mDofVels[1][0] = 0.005 / (numIter * mTimeStep);;
                mDofVels[1][2] = -0.005 / (numIter * mTimeStep);;
            }
            
            if (mSimFrame == numIter * 45) {        
                mSkels[1]->setKinematicState(true);
                mSkels[2]->setKinematicState(true);
                mCollisionHandle->reset();
                mDofs[1][8] = -1.0;
                mDofs[1][9] = 0.9;
            }
            if(mSimFrame > numIter * 45 && mSimFrame < numIter * 90) {    
                mDofs[2][0] -= 0.01 / numIter;
                mDofs[2][1] += 0.008 / numIter;
                mDofVels[2][0] = -0.01 /(numIter * mTimeStep);
                mDofVels[2][1] = 0.008 /(numIter * mTimeStep);
                mDofs[1][0] -= 0.01 / numIter;
                mDofs[1][1] += 0.008 / numIter;
                mDofVels[1][0] = -0.01 / (numIter * mTimeStep);
                mDofVels[1][1] = 0.008 / (numIter * mTimeStep);
            }
            
            if(mSimFrame > numIter * 90 && mSimFrame < numIter * 110) {    
                mDofs[2][0] += 0.015 / numIter;
                mDofs[2][1] -= 0.008 / numIter;
                mDofs[2][4] -= 0.009 / numIter;
                mDofVels[2][0] = 0.015 /(numIter * mTimeStep);
                mDofVels[2][1] = -0.008 /(numIter * mTimeStep);
                mDofVels[2][4] = -0.009 /(numIter * mTimeStep);
                mDofs[1][0] += 0.015 / numIter;
                mDofs[1][1] -= 0.008 / numIter;
                mDofs[1][4] -= 0.009 / numIter;
                mDofVels[1][0] = 0.015 / (numIter * mTimeStep);
                mDofVels[1][1] = -0.008 / (numIter * mTimeStep);
                mDofVels[1][4] = -0.009 / (numIter * mTimeStep);
            }
            if (mSimFrame == numIter * 110 && i == 0) {
                mDofVels[2][0] = 0.015 / (numIter * mTimeStep);
                mDofVels[2][1] = -0.008 / (numIter * mTimeStep);
                mDofVels[2][4] = -0.009 / (numIter * mTimeStep);
                mDofVels[2][5] = -0.09 / (numIter * mTimeStep);
                mSkels[2]->setKinematicState(false);
                mCollisionHandle->reset();
                }*/

            bake();
        }

        mForce = Vector3d::Zero();
        mSimFrame += numIter;

        glutPostRedisplay();
        glutTimerFunc(mDisplayTimeout, refreshTimer, _val);
    }
}

void MyWindow::draw()
{
    glDisable(GL_LIGHTING);

    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    if (!mSim) {
        if (mPlayFrame < mBakedStates.size()) {
            for (unsigned int i = 0; i < mSkels.size(); i++) {
                int start = mIndices[i];
                int size = mDofs[i].size();
                mSkels[i]->setPose(mBakedStates[mPlayFrame].segment(start, size), false, false);
            }
            if (mShowMarkers) {
                int sumDofs = mIndices[mSkels.size()]; 
                int nContact = (mBakedStates[mPlayFrame].size() - sumDofs) / 6;
                for (int i = 0; i < nContact; i++) {
                    Vector3d v = mBakedStates[mPlayFrame].segment(sumDofs + i * 6, 3);
                    Vector3d n = mBakedStates[mPlayFrame].segment(sumDofs + i * 6 + 3, 3) / 10.0;
                    glBegin(GL_LINES);
                    glVertex3f(v[0], v[1], v[2]);
                    glVertex3f(v[0] + n[0], v[1] + n[1], v[2] + n[2]);
                    glEnd();
                    mRI->setPenColor(Vector3d(0.2, 0.2, 0.8));
                    mRI->pushMatrix();
                    glTranslated(v[0], v[1], v[2]);
                    mRI->drawEllipsoid(Vector3d(0.02, 0.02, 0.02));
                    mRI->popMatrix();
                }
            }
        }
    }else{
        if (mShowMarkers) {
            for (int k = 0; k < mCollisionHandle->getCollisionChecker()->getNumContact(); k++) {
                Vector3d  v = mCollisionHandle->getCollisionChecker()->getContact(k).point;
                Vector3d n = mCollisionHandle->getCollisionChecker()->getContact(k).normal / 10.0;
                Vector3d f = mCollisionHandle->getCollisionChecker()->getContact(k).force / 10.0;

                glBegin(GL_LINES);
                glVertex3f(v[0], v[1], v[2]);
                glVertex3f(v[0] + f[0], v[1] + f[1], v[2] + f[2]);
                glEnd();
                mRI->setPenColor(Vector3d(0.2, 0.2, 0.8));
                mRI->pushMatrix();
                glTranslated(v[0], v[1], v[2]);
                mRI->drawEllipsoid(Vector3d(0.02, 0.02, 0.02));
                mRI->popMatrix();
            }
        }
    }

    for (unsigned int i = 0; i < mSkels.size(); i++)
        mSkels[i]->draw(mRI);
        
    // display the frame count in 2D text
    char buff[64];
    if (!mSim) 
        sprintf(buff, "%d", mPlayFrame);
    else
        sprintf(buff, "%d", mSimFrame);
    string frame(buff);
    glDisable(GL_LIGHTING);
    glColor3f(0.0,0.0,0.0);
    yui::drawStringOnScreen(0.02f,0.02f,frame);
    glEnable(GL_LIGHTING);
}

void MyWindow::keyboard(unsigned char key, int x, int y)
{
    switch(key){
    case ' ': // use space key to play or stop the motion
        mSim = !mSim;
        if (mSim) {
            mPlay = false;
            glutTimerFunc( mDisplayTimeout, refreshTimer, 0);
        }
        break;
    case 's': // simulate one frame
        if (!mPlay) {
            mForce = Vector3d::Zero();
            setPose();
            mIntegrator.integrate(this, mTimeStep);
            mSimFrame++;
            bake();
            glutPostRedisplay();
        }
        break;
    case '1': // upper right force
        mForce[0] = 0.4;
        mForce[1] = 0.8;
        cout << "push up and right" << endl;
        break;
    case '2': // upper right force
        mForce[0] = -0.4;
        cout << "push left" << endl;
        break;
    case 'p': // playBack
        mPlay = !mPlay;
        if (mPlay) {
            mSim = false;
            glutTimerFunc( mDisplayTimeout, refreshTimer, 0);
        }
        break;
    case '[': // step backward
        if (!mSim) {
            mPlayFrame--;
            if(mPlayFrame < 0)
                mPlayFrame = 0;
            glutPostRedisplay();
        }
        break;
    case ']': // step forwardward
        if (!mSim) {
            mPlayFrame++;
            if(mPlayFrame >= mBakedStates.size())
                mPlayFrame = 0;
            glutPostRedisplay();
        }
        break;
    case 'v': // show or hide markers
        mShowMarkers = !mShowMarkers;
        break;
    default:
        Win3D::keyboard(key,x,y);
    }
    glutPostRedisplay();
}

void MyWindow::bake()
{
    int nContact = mCollisionHandle->getCollisionChecker()->getNumContact();
    VectorXd state(mIndices.back() + 6 * nContact);
    for (unsigned int i = 0; i < mSkels.size(); i++)
        state.segment(mIndices[i], mDofs[i].size()) = mDofs[i];
    for (int i = 0; i < nContact; i++) {
        int begin = mIndices.back() + i * 6;
        state.segment(begin, 3) = mCollisionHandle->getCollisionChecker()->getContact(i).point;
        state.segment(begin + 3, 3) = mCollisionHandle->getCollisionChecker()->getContact(i).normal;
    }

    mBakedStates.push_back(state);
}